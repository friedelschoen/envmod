#define _GNU_SOURCE

#include "arg.h"
#include "signames.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <linux/limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEFAULT_SHELL "sh"

#define ENVFILE_MAX 16
#define KEEPENV_MAX 64

#define FAIL_ERRNO(exitcode, fmt, ...) \
	(fprintf(stderr, "%s: " fmt ": %s\n", self, ##__VA_ARGS__, strerror(errno)), exitcode > -1 ? exit(exitcode) : 0)


extern char      **environ;
static int         sigign[NSIG];
static const char *sigtrap[NSIG];
static pid_t       pid;
static char       *self;


/* uid:gid[:gid[:gid]...] */
static int parse_ugid_num(char *str, uid_t *uid, gid_t *gids) {
	int   i = 0;
	char *end;

	*uid = strtoul(str, &end, 10);
	if (*end != ':') {
		gids[0] = *uid;
		return 1;
	}

	str = end + 1;

	while (i < 60) {
		gids[i++] = strtoul(str, &end, 10);

		if (*end != ':')
			break;

		str = end + 1;
	}

	if (*end != '\0') {
		fprintf(stderr, "%s: expected end in uidgid, got %c\n", self, *end);
		exit(100);
	}

	return i;
}

static int parse_ugid(char *str, uid_t *uid, gid_t *gids) {
	struct passwd *pwd;
	struct group  *gr;
	char          *end;
	char          *groupstr = NULL;
	int            gid_size = 0;
	char          *next;

	if (str[0] == ':')
		return parse_ugid_num(str + 1, uid, gids);

	if ((end = strchr(str, ':')) != NULL) {
		end[0]   = '\0';
		groupstr = end + 1;
	}

	if ((pwd = getpwnam(str)) == NULL) {
		fprintf(stderr, "%s: unknown user: %s\n", self, str);
		exit(101);
	}
	*uid = pwd->pw_uid;

	if (groupstr == NULL) {
		gids[0] = pwd->pw_gid;
		return 1;
	}

	next = groupstr;

	while (next && gid_size < 60) {
		groupstr = next;
		if ((end = strchr(groupstr, ':')) != NULL) {
			end[0] = '\0';
			next   = end + 1;
		} else {
			next = NULL;
		}
		if ((gr = getgrnam(groupstr)) == NULL) {
			fprintf(stderr, "%s: unknown group: %s\n", self, groupstr);
			exit(101);
		}
		gids[gid_size++] = gr->gr_gid;
	}

	return gid_size;
}

static char *strip(char *text, size_t size) {
	while (size > 0 && isspace(text[0])) {
		text++, size--;
	}
	while (size > 0 && isspace(text[size] - 1)) {
		size--;
	}
	text[size] = '\0';
	return text;
}

static char *nicevar(char *text, size_t size) {
	text = strip(text, size);

	for (size_t i = 0; i < size; i++) {
		if (text[i] == '=') {
			fprintf(stderr, "'=' in envfile: %s\n", text);
			return NULL;
		}
		if (text[i] == '\0') {
			text[i] = '\n';
		}
	}

	return text;
}

static void parse_envdir(const char *path) {
	DIR           *dir;
	FILE          *fp;
	struct dirent *entry;
	char           entrypath[PATH_MAX], *envval = NULL, *newval;
	long           envvalalloc = 0, size;

	if (!(dir = opendir(path))) {
		FAIL_ERRNO(101, "unable to open envdir `%s`", path);
	}

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		snprintf(entrypath, PATH_MAX, "%s/%s", path, entry->d_name);
		if ((fp = fopen(entrypath, "r")) == NULL) {
			FAIL_ERRNO(-1, "unable to open `%s`", entrypath);
			continue;
		}
		fseek(fp, 0, SEEK_END);
		size = ftell(fp);
		if (size == 0) {
			unsetenv(entry->d_name);
		} else {
			rewind(fp);
			if (size + 1 > envvalalloc) {
				if ((newval = realloc(envval, size + 1)) == NULL) {
					FAIL_ERRNO(102, "unable to allocate memory");
				}
				envval      = newval;
				envvalalloc = size + 1;
			}
			fread(envval, size, 1, fp);
			if ((newval = nicevar(envval, size)) != NULL) {
				setenv(entry->d_name, newval, 1);
			}
		}
		fclose(fp);
	}
	if (envval != NULL)
		free(envval);

	closedir(dir);
}

static void parse_envfile(const char *path) {
	FILE   *fp;
	char   *line       = NULL, *value;
	size_t  line_alloc = 0;
	ssize_t line_len;

	if ((fp = fopen(path, "r")) == NULL) {
		FAIL_ERRNO(101, "unable to open envfile `%s`", path);
	}

	while ((line_len = getline(&line, &line_alloc, fp)) > 0) {
		line  = strip(line, line_len);
		value = strchr(line, '=');
		if (!value)
			continue;

		*(value++) = '\0';
		if (value[0] == '\0') {
			unsetenv(line);
		} else {
			setenv(line, value, 1);
		}
	}

	if (line)
		free(line);
	fclose(fp);
}

static void limit(int what, long l) {
	struct rlimit r;

	if (getrlimit(what, &r) == -1) {
		FAIL_ERRNO(102, "unable to get rlimit");
	}
	if (l < 0) {
		r.rlim_cur = 0;
	} else if ((rlim_t) l > r.rlim_max)
		r.rlim_cur = r.rlim_max;
	else
		r.rlim_cur = l;

	if (setrlimit(what, &r) == -1)
		FAIL_ERRNO(102, "unable to set rlimit");
}

static char *shellname(void) {
	char *name = getenv("SHELL");
	if (name)
		return name;
	return DEFAULT_SHELL;
}

static void usage() {
	fprintf(stderr, "usage: envmod [options] prog [arguments...]\n"
	                "       softlimit [options] prog [arguments...]\n"
	                "       setlock [-nNxX] prog [arguments...]\n"
	                "       setuidgid [:]user[:group] prog [arguments...]\n"
	                "       envuidgid [:]user[:group] prog [arguments...]\n"
	                "       envdir dir prog [arguments...]\n"
	                "       pgrphack prog [arguments...]\n");

	exit(100);
}

static void signal_handler(int signo) {
	if (signo == SIGCHLD)
		return;

	if (sigtrap[signo]) {
		char        signo_str[10];
		const char *shell = shellname();
		snprintf(signo_str, sizeof(signo_str), "%d", signo);

		pid_t shellpid;
		while ((shellpid = fork()) == -1) {
			FAIL_ERRNO(-1, "unable to fork, retrying");
			sleep(1);
		}

		if (shellpid == 0) {
			setenv("signo", signo_str, 1);
			setenv("signame", signum_to_signame(signo), 1);
			execlp(shell, shell, "-c", sigtrap[signo], NULL);
			FAIL_ERRNO(0, "unable to execute shell");
			_exit(127);
		}
	}

	if (sigign[signo])
		return;

	kill(pid, signo);
}

int main(int argc, char **argv) {
	int   lockfd, lockfdflags = 0, lockflags = 0, locktimeout = 0, gid_len = 0, envgid_len = 0, useshell = 0;
	char *arg0 = NULL, *root = NULL, *cd = NULL, *lock = NULL, *exec = NULL;
	char *envdirpath[ENVFILE_MAX], *envfilepath[ENVFILE_MAX], *modenv[KEEPENV_MAX];
	int   dofork         = 0;
	int   envdirpath_len = 0, envfilepath_len = 0, modenv_len = 0;
	int   setuser = 0, setenvuser = 0, clearenviron = 0, setenvargs = 0;
	uid_t uid, envuid;
	gid_t gid[61], envgid[61];
	long  limitd = -2, limits = -2, limitl = -2, limita = -2, limito = -2, limitp = -2, limitf = -2, limitc = -2,
	     limitr = -2, limitt = -2;
	long nicelevel = 0;
	int  ssid      = 0;
	int  verbose   = 0;
	int  closefd[10];
	for (int i = 0; i < 10; i++)
		closefd[i] = 0;
	self = strrchr(argv[0], '/');
	if (self == NULL)
		self = argv[0];
	else
		self++;

	for (int i = 0; i < NSIG; i++)
		sigign[i] = 0;
	for (int i = 0; i < NSIG; i++)
		sigtrap[i] = NULL;

	if (!strcmp(self, "setuidgid") || !strcmp(self, "envuidgid")) {
		if (argc < 2) {
			fprintf(stderr, "%s <uid-gid> command...", self);
			return 100;
		}
		setuser++;
		gid_len = parse_ugid(argv[1], &uid, gid);
		argv += 2, argc -= 2;
	} else if (!strcmp(self, "envdir")) {
		if (argc < 2) {
			fprintf(stderr, "%s <uid-gid> command...", self);
			return 100;
		}
		envdirpath[envdirpath_len++] = argv[1];
		argv += 2, argc -= 2;
	} else if (!strcmp(self, "pgrphack")) {
		ssid++;
		SHIFT;
	} else if (!strcmp(self, "setlock")) {
		ARGBEGIN
		switch (OPT) {
			case 'n':
				lockflags = LOCK_EX | LOCK_NB;
				break;
			case 'N':
				lockflags = LOCK_EX;
				break;
			case 'x':
			case 'X':
				fprintf(stderr, "warning: '-%c' is ignored\n", OPT);
				break;
			default:
				fprintf(stderr, "error: unknown option -%c\n", OPT);
				usage();
		}
		ARGEND
		lock = argv[0];
		SHIFT;
	} else if (!strcmp(self, "softlimit")) {
		ARGBEGIN
		switch (OPT) {
			case 'm':
				limits = limitl = limita = limitd = atol(EARGF(usage()));
				break;
			case 'a':
				limita = atol(EARGF(usage()));
				break;
			case 'd':
				limitd = atol(EARGF(usage()));
				break;
			case 'o':
				limito = atol(EARGF(usage()));
				break;
			case 'p':
				limitp = atol(EARGF(usage()));
				break;
			case 'f':
				limitf = atol(EARGF(usage()));
				break;
			case 'c':
				limitc = atol(EARGF(usage()));
				break;
			case 'r':
				limitr = atol(EARGF(usage()));
				break;
			case 't':
				limitt = atol(EARGF(usage()));
				break;
			case 'l':
			case 'M':
				limitl = atol(EARGF(usage()));
				break;
			case 's':
				limits = atol(EARGF(usage()));
				break;
			default:
				fprintf(stderr, "error: unknown option -%c\n", OPT);
				usage();
		}
		ARGEND
	} else if (!strcmp(self, "env")) {
		ARGBEGIN
		switch (OPT) {
			case 'i':
				clearenviron++;
				break;
			case 'u':
				modenv[modenv_len++] = EARGF(usage());
				break;
			case 'C':
				cd = EARGF(usage());
				break;
			case 'v':
				verbose++;
				break;
			default:
				fprintf(stderr, "error: unknown option -%c\n", OPT);
				usage();
		}
		ARGEND
		setenvargs++;
	} else if (!strcmp(self, "flock")) {
		lockflags   = LOCK_EX;
		lockfdflags = 0;
		ARGBEGIN
		switch (OPT) {
			case 'c':
				useshell++;
				break;
			case 'e':
			case 'x':
				lockflags &= ~LOCK_SH;
				lockflags |= LOCK_EX;
				break;
			case 'n':
				lockflags |= LOCK_NB;
				break;
			case 'o':
				lockfdflags |= O_CLOEXEC;
				break;
			case 's':
				lockflags &= ~LOCK_EX;
				lockflags |= LOCK_SH;
				break;
			case 'v':
				verbose++;
				break;
			case 'w':
				locktimeout = (int) (1000.0 * atof(EARGF(usage())));
				break;
			case 'E':
			case 'F':
			case 'u':
				fprintf(stderr, "warning: option -%c is ignored\n", OPT);
				break;
			default:
				fprintf(stderr, "error: unknown option -%c\n", OPT);
				usage();
		}
		ARGEND
	} else {
		if (strcmp(self, "envmod") && strcmp(self, "chpst"))
			fprintf(stderr, "warning: program-name unsupported, assuming `envmod`\n");

		ARGBEGIN
		switch (OPT) {
			case 'u':
				setuser++;
				if ((gid_len = parse_ugid(EARGF(usage()), &uid, gid)) == -1) {
					return -1;
				}
				break;
			case 'U':
				setenvuser++;
				if ((envgid_len = parse_ugid(EARGF(usage()), &envuid, envgid)) == -1) {
					return -1;
				}
				break;
			case 'b':
				arg0 = EARGF(usage());
				break;
			case '/':
				root = EARGF(usage());
				break;
			case 'C':
				cd = EARGF(usage());
				break;
			case 'n':
				nicelevel = atol(EARGF(usage()));
				break;
			case 'l':
				lock      = EARGF(usage());
				lockflags = LOCK_EX | LOCK_NB;
				break;
			case 'L':
				lock      = EARGF(usage());
				lockflags = LOCK_EX;
				break;
			case 'e':
				envdirpath[envdirpath_len++] = EARGF(usage());
				break;
			case 'E':
				envfilepath[envfilepath_len++] = EARGF(usage());
				break;
			case 'x':
				clearenviron++;
				break;
			case 'k':
				modenv[modenv_len++] = EARGF(usage());
				break;
			case 'v':
				verbose++;
				break;
			case 'P':
				ssid++;
				break;
			case 'S':
				useshell++;
				break;
			case '0' ... '9':
				closefd[OPT - '0']++;
				break;
			case 'i':
				dofork++;
				sigign[signame_to_signum(EARGF(usage()))]++;
				break;
			case 'T':
				dofork++;
				int         signo   = signame_to_signum(EARGF(usage()));
				const char *command = EARGF(usage());

				sigtrap[signo] = command;
				break;
			case 'F':
				dofork++;
				break;
			case 'm':
				limits = limitl = limita = limitd = atol(EARGF(usage()));
				break;
			case 'd':
				limitd = atol(EARGF(usage()));
				break;
			case 'o':
				limito = atol(EARGF(usage()));
				break;
			case 'p':
				limitp = atol(EARGF(usage()));
				break;
			case 'f':
				limitf = atol(EARGF(usage()));
				break;
			case 'c':
				limitc = atol(EARGF(usage()));
				break;
			case 'r':
				limitr = atol(EARGF(usage()));
				break;
			case 't':
				limitt = atol(EARGF(usage()));
				break;
			case 's':
				limits = atol(EARGF(usage()));
				break;
			default:
				fprintf(stderr, "error: unknown option -%c\n", OPT);
				usage();
		}
		ARGEND

		setenvargs++;
	}

	if (setenvargs) {
		while (argc > 0) {
			char *value = strchr(argv[0], '=');
			if (!value)
				break;
			*(value++) = '\0';
			setenv(argv[0], value, 1);
			SHIFT;
		}
	}

	if (argc == 0) {
		fprintf(stderr, "%s: command required\n", self);
		usage();
	}

	if (ssid) {
		if (setsid()) {
			FAIL_ERRNO(101, "unable to set sid");
		}
	}

	if (setuser) {
		if (setgroups(gid_len, gid) == -1) {
			FAIL_ERRNO(101, "unable to set groups");
		}
		if (setgid(gid[0]) == -1) {
			FAIL_ERRNO(101, "unable to set user-group");
		}
		if (setuid(uid) == -1) {
			FAIL_ERRNO(101, "unable to set user");
		}

		if (envuid == 0) {
			setenvuser++;
			envuid     = uid;
			envgid_len = gid_len;
			memcpy(envgid, gid, sizeof(*gid) * gid_len);
		}
	}

	if (setenvuser) {
		char dest[10];
		snprintf(dest, sizeof(dest), "%u", envuid);
		setenv("UID", dest, 1);
		snprintf(dest, sizeof(dest), "%u", envgid[0]);
		setenv("GID", dest, 1);
	}

	if (root) {
		if (chroot(root) == -1)
			FAIL_ERRNO(101, "unable to change root-directory");

		/* chdir to '/', otherwise the next command will complain 'directory not found' */
		if (chdir("/") == -1)
			FAIL_ERRNO(101, "unable to change directory");
	}

	if (cd) {
		if (chdir(cd) == -1)
			FAIL_ERRNO(101, "unable to change directory");
	}

	if (nicelevel != 0) {
		errno = 0;
		/* don't check return-value, nice(2) states there are true negatives,
		   checking errno is more consistent */
		nice(nicelevel);
		if (errno != 0) {
			FAIL_ERRNO(101, "unable to set nice level");
		}
	}

	if (limitd >= -1) {
#ifdef RLIMIT_DATA
		limit(RLIMIT_DATA, limitd);
#else
		if (verbose)
			fprintf(stderr, "%s: system does not support RLIMIT_DATA\n", self);
#endif
	}
	if (limits >= -1) {
#ifdef RLIMIT_STACK
		limit(RLIMIT_STACK, limits);
#else
		if (verbose)
			fprintf(stderr, "%s: system does not support RLIMIT_STACK\n", self);
#endif
	}
	if (limitl >= -1) {
#ifdef RLIMIT_MEMLOCK
		limit(RLIMIT_MEMLOCK, limitl);
#else
		if (verbose)
			fprintf(stderr, "%s: system does not support RLIMIT_MEMLOCK\n", self);
#endif
	}
	if (limita >= -1) {
#ifdef RLIMIT_VMEM
		limit(RLIMIT_VMEM, limita);
#else
#	ifdef RLIMIT_AS
		limit(RLIMIT_AS, limita);
#	else
		if (verbose)
			fprintf(stderr, "%s: system does neither support RLIMIT_VMEM nor RLIMIT_AS\n", self);
#	endif
#endif
	}
	if (limito >= -1) {
#ifdef RLIMIT_NOFILE
		limit(RLIMIT_NOFILE, limito);
#else
#	ifdef RLIMIT_OFILE
		limit(RLIMIT_OFILE, limito);
#	else
		if (verbose)
			fprintf(stderr, "%s: system does neither support RLIMIT_NOFILE nor RLIMIT_OFILE\n", self);
#	endif
#endif
	}
	if (limitp >= -1) {
#ifdef RLIMIT_NPROC
		limit(RLIMIT_NPROC, limitp);
#else
		if (verbose)
			fprintf(stderr, "%s: system does not support RLIMIT_NPROC\n", self);
#endif
	}
	if (limitf >= -1) {
#ifdef RLIMIT_FSIZE
		limit(RLIMIT_FSIZE, limitf);
#else
		if (verbose)
			fprintf(stderr, "%s: system does not support RLIMIT_FSIZE\n", self);
#endif
	}
	if (limitc >= -1) {
#ifdef RLIMIT_CORE
		limit(RLIMIT_CORE, limitc);
#else
		if (verbose)
			fprintf(stderr, "%s: system does not support RLIMIT_CORE\n", self);
#endif
	}
	if (limitr >= -1) {
#ifdef RLIMIT_RSS
		limit(RLIMIT_RSS, limitr);
#else
		if (verbose)
			fprintf(stderr, "%s: system does not support RLIMIT_RSS\n", self);
#endif
	}
	if (limitt >= -1) {
#ifdef RLIMIT_CPU
		limit(RLIMIT_CPU, limitt);
#else
		if (verbose)
			fprintf(stderr, "%s: system does not support RLIMIT_CPU\n", self);
#endif
	}

	if (lock) {
		if ((lockfd = open(lock, lockfdflags | O_WRONLY | O_APPEND | O_CREAT, 0644)) == -1)
			FAIL_ERRNO(101, "unable to open lockfile");

		if (locktimeout)
			ualarm(locktimeout * 1000, 0);

		if (flock(lockfd, lockflags) == -1)
			FAIL_ERRNO(101, "unable to lock file");

		/* cancel alarm */
		alarm(0);
	}

	if (clearenviron) {
		if (modenv_len == 0) {
			clearenv();
		} else {
			char **newenvion = malloc((modenv_len + 1) * sizeof(char *));
			int    envlen    = 0;
			for (int i = 0; i < modenv_len; i++) {
				char *value = getenv(modenv[i]);
				if (value == NULL) {
					fprintf(stderr, "%s: unknown environ-var '%s'\n", self, modenv[i]);
					continue;
				}
				char *pair = malloc(strlen(modenv[i]) + strlen(value) + 2);
				sprintf(pair, "%s=%s", modenv[i], value);
				newenvion[envlen++] = pair;
			}
			newenvion[envlen] = NULL;
			environ           = newenvion;
		}
	} else {
		for (int i = 0; i < modenv_len; i++) {
			unsetenv(modenv[i]);
		}
	}

	for (int i = 0; i < envdirpath_len; i++) {
		parse_envdir(envdirpath[i]);
	}

	for (int i = 0; i < envfilepath_len; i++) {
		parse_envfile(envfilepath[i]);
	}

	for (int i = 0; i < 10; i++) {
		if (closefd[i] && close(i) == -1) {
			FAIL_ERRNO(101, "unable to close fd %d", i);
		}
	}

	exec = argv[0];
	if (arg0)
		argv[0] = arg0;

	if (useshell) {
		char **newargv = malloc((argc + 3) * sizeof(char *));
		newargv[0]     = shellname();
		newargv[1]     = "-c";
		for (int i = 0; i < argc + 1; i++)
			newargv[i + 2] = argv[i];
		argv = newargv;
		argc += 2;
	}

	if (!dofork) {
		execvpe(exec, argv, environ);
		FAIL_ERRNO(127, "unable to execute");
	}

	for (int i = 0; i < NSIG; i++)
		signal(i, signal_handler);

	while ((pid = fork()) == -1) {
		FAIL_ERRNO(-1, "unable to fork, retrying");
		sleep(1);
	}

	if (pid == 0) {
		execvpe(exec, argv, environ);
		FAIL_ERRNO(-1, "unable to execute");
		_exit(127);
	}

	int exitstat;
	waitpid(pid, &exitstat, 0);
	if (WIFEXITED(exitstat)) {
		if (verbose)
			fprintf(stderr, "%s: child exited %d\n", self, WEXITSTATUS(exitstat));
		return WEXITSTATUS(exitstat);
	}

	if (WIFSIGNALED(exitstat)) {
		fprintf(stderr, "%s: child terminated using %s\n", self, signum_to_signame(WTERMSIG(exitstat)));
		return 120;
	}

	fprintf(stderr, "%s: child terminated\n", self);
	return 121;
}
