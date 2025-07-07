#define _GNU_SOURCE

#include "arg.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <linux/limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>

#define ENVFILE_MAX 16


extern char **environ;

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
		fprintf(stderr, "error: expected end, got %c\n", *end);
		return -1;
	}

	return i;
}

int parse_ugid(char *str, uid_t *uid, gid_t *gids) {
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
		fprintf(stderr, "unknown user: %s\n", groupstr);
		return -1;
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
			fprintf(stderr, "unknown group: %s\n", groupstr);
			return -1;
		}
		gids[gid_size++] = gr->gr_gid;
	}

	return gid_size;
}

char *strip(char *text, size_t size) {
	while (size > 0 && isspace(text[0])) {
		text++, size--;
	}
	while (size > 0 && isspace(text[size] - 1)) {
		size--;
	}
	text[size] = '\0';
	return text;
}

char *nicevar(char *text, size_t size) {
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

void parse_envdir(const char *path) {
	DIR           *dir;
	FILE          *fp;
	struct dirent *entry;
	char           entrypath[PATH_MAX], *envval = NULL, *newval;
	size_t         size, envvalalloc            = 0;

	if (!(dir = opendir(path))) {
		perror("opendir envdir");
		return;
	}

	while ((entry = readdir(dir)) != NULL) {
		snprintf(entrypath, PATH_MAX, "%s/%s", path, entry->d_name);
		if ((fp = fopen(entrypath, "r")) == NULL) {
			fprintf(stderr, "unable to open %s: %s\n", entrypath, strerror(errno));
			perror("fopen envdir file");
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
					perror("alloc");
					exit(1);
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

void parse_envfile(const char *path) {
	FILE   *fp;
	char   *line       = NULL, *value;
	size_t  line_alloc = 0;
	ssize_t line_len;

	if ((fp = fopen(path, "r")) == NULL) {
		perror("open envfile");
		exit(1);
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

void limit(int what, long l) {
	struct rlimit r;

	if (getrlimit(what, &r) == -1) {
		fprintf(stderr, "error: unable to getrlimit\n");
		return;
	}
	if (l < 0) {
		r.rlim_cur = 0;
	} else if ((rlim_t) l > r.rlim_max)
		r.rlim_cur = r.rlim_max;
	else
		r.rlim_cur = l;

	if (setrlimit(what, &r) == -1)
		fprintf(stderr, "error: unable to setrlimit\n");
}

void usage(int code) {
	fprintf(stderr, "usage: envmod [options] prog [arguments...]\n"
	                "       softlimit [options] prog [arguments...]\n"
	                "       setlock [-nNxX] prog [arguments...]\n"
	                "       setuidgid [:]user[:group] prog [arguments...]\n"
	                "       envuidgid [:]user[:group] prog [arguments...]\n"
	                "       envdir dir prog [arguments...]\n"
	                "       pgrphack prog [arguments...]\n");

	exit(code);
}

int main(int argc, char **argv) {
	int   lockfd, lockflags = 0, gid_len = 0, envgid_len = 0;
	char *arg0 = NULL, *root = NULL, *cd = NULL, *lock = NULL, *exec = NULL;
	char *envdirpath[ENVFILE_MAX], *envfilepath[ENVFILE_MAX];
	int   envdirpath_len = 0, envfilepath_len = 0;
	int   setuser = 0, setenvuser = 0, clearenviron = 0;
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
	char *self = strrchr(argv[0], '/');
	if (self == NULL)
		self = argv[0];
	else
		self++;

	if (!strcmp(self, "setuidgid") || !strcmp(self, "envuidgid")) {
		if (argc < 2) {
			fprintf(stderr, "%s <uid-gid> command...", self);
			return 1;
		}
		setuser++;
		gid_len = parse_ugid(argv[1], &uid, gid);
		argv += 2, argc -= 2;
	} else if (!strcmp(self, "envdir")) {
		if (argc < 2) {
			fprintf(stderr, "%s <uid-gid> command...", self);
			return 1;
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
				usage(1);
		}
		ARGEND
		lock = argv[0];
		SHIFT;
	} else if (!strcmp(self, "softlimit")) {
		ARGBEGIN
		switch (OPT) {
			case 'm':
				limits = limitl = limita = limitd = atol(EARGF(usage(1)));
				break;
			case 'a':
				limita = atol(EARGF(usage(1)));
				break;
			case 'd':
				limitd = atol(EARGF(usage(1)));
				break;
			case 'o':
				limito = atol(EARGF(usage(1)));
				break;
			case 'p':
				limitp = atol(EARGF(usage(1)));
				break;
			case 'f':
				limitf = atol(EARGF(usage(1)));
				break;
			case 'c':
				limitc = atol(EARGF(usage(1)));
				break;
			case 'r':
				limitr = atol(EARGF(usage(1)));
				break;
			case 't':
				limitt = atol(EARGF(usage(1)));
				break;
			case 'l':
			case 'M':
				limitl = atol(EARGF(usage(1)));
				break;
			case 's':
				limits = atol(EARGF(usage(1)));
				break;
			default:
				fprintf(stderr, "error: unknown option -%c\n", OPT);
				usage(1);
		}
		ARGEND
	} else {
		if (strcmp(self, "envmod") && strcmp(self, "chpst"))
			fprintf(stderr, "warning: program-name unsupported, assuming `envmod`\n");

		ARGBEGIN
		switch (OPT) {
			case 'u':
				setuser++;
				if ((gid_len = parse_ugid(EARGF(usage(1)), &uid, gid)) == -1) {
					return -1;
				}
				break;
			case 'U':
				setenvuser++;
				if ((envgid_len = parse_ugid(EARGF(usage(1)), &envuid, envgid)) == -1) {
					return -1;
				}
				break;
			case 'b':
				arg0 = EARGF(usage(1));
				break;
			case '/':
				root = EARGF(usage(1));
				break;
			case 'C':
				cd = EARGF(usage(1));
				break;
			case 'n':
				nicelevel = atol(EARGF(usage(1)));
				break;
			case 'l':
				lock      = EARGF(usage(1));
				lockflags = LOCK_EX | LOCK_NB;
				break;
			case 'L':
				lock      = EARGF(usage(1));
				lockflags = LOCK_EX;
				break;
			case 'e':
				envdirpath[envdirpath_len++] = EARGF(usage(1));
				break;
			case 'E':
				envfilepath[envfilepath_len++] = EARGF(usage(1));
				break;
			case 'x':
				clearenviron++;
				break;
			case 'v':
				verbose++;
				break;
			case 'P':
				ssid++;
				break;
			case '0' ... '9':
				closefd[OPT - '0']++;
				break;
			case 'm':
				limits = limitl = limita = limitd = atol(EARGF(usage(1)));
				break;
			case 'd':
				limitd = atol(EARGF(usage(1)));
				break;
			case 'o':
				limito = atol(EARGF(usage(1)));
				break;
			case 'p':
				limitp = atol(EARGF(usage(1)));
				break;
			case 'f':
				limitf = atol(EARGF(usage(1)));
				break;
			case 'c':
				limitc = atol(EARGF(usage(1)));
				break;
			case 'r':
				limitr = atol(EARGF(usage(1)));
				break;
			case 't':
				limitt = atol(EARGF(usage(1)));
				break;
			case 's':
				limits = atol(EARGF(usage(1)));
				break;
			default:
				fprintf(stderr, "error: unknown option -%c\n", OPT);
				usage(1);
		}
		ARGEND
	}

	if (argc == 0) {
		fprintf(stderr, "%s: command required\n", self);
		usage(1);
	}

	if (ssid) {
		if (setsid()) {
			perror("setsid");
			return 1;
		}
	}

	if (setuser) {
		if (setgroups(gid_len, gid) == -1) {
			perror("setgroups");
			return 1;
		}
		if (setgid(gid[0]) == -1) {
			perror("setgid");
			return 1;
		}
		if (setuid(uid) == -1) {
			perror("setuid");
			return 1;
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
		if (chroot(root) == -1) {
			perror("unable to change root directory");
			exit(1);
		}
		// chdir to '/', otherwise the next command will complain 'directory not found'
		if (chdir("/") == -1)
			perror("unable to change directory");
	}

	if (cd) {
		if (chdir(cd) == -1) {
			perror("unable to change directory");
			exit(1);
		}
	}

	if (nicelevel != 0) {
		errno = 0;
		nice(nicelevel);
		if (errno != 0) {
			perror("unable to set nice level");
			exit(1);
			/* no exit, nice(2) states there are true negatives */
		}
	}

	if (limitd >= -1) {
#ifdef RLIMIT_DATA
		limit(RLIMIT_DATA, limitd);
#else
		if (verbose)
			fprintf(stderr, "system does not support RLIMIT_DATA\n");
#endif
	}
	if (limits >= -1) {
#ifdef RLIMIT_STACK
		limit(RLIMIT_STACK, limits);
#else
		if (verbose)
			fprintf(stderr, "system does not support RLIMIT_STACK\n");
#endif
	}
	if (limitl >= -1) {
#ifdef RLIMIT_MEMLOCK
		limit(RLIMIT_MEMLOCK, limitl);
#else
		if (verbose)
			fprintf(stderr, "system does not support RLIMIT_MEMLOCK\n");
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
			fprintf(stderr, "system does neither support RLIMIT_VMEM nor RLIMIT_AS\n");
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
			fprintf(stderr, "system does neither support RLIMIT_NOFILE nor RLIMIT_OFILE\n");
#	endif
#endif
	}
	if (limitp >= -1) {
#ifdef RLIMIT_NPROC
		limit(RLIMIT_NPROC, limitp);
#else
		if (verbose)
			fprintf(stderr, "system does not support RLIMIT_NPROC\n");
#endif
	}
	if (limitf >= -1) {
#ifdef RLIMIT_FSIZE
		limit(RLIMIT_FSIZE, limitf);
#else
		if (verbose)
			fprintf(stderr, "system does not support RLIMIT_FSIZE\n");
#endif
	}
	if (limitc >= -1) {
#ifdef RLIMIT_CORE
		limit(RLIMIT_CORE, limitc);
#else
		if (verbose)
			fprintf(stderr, "system does not support RLIMIT_CORE\n");
#endif
	}
	if (limitr >= -1) {
#ifdef RLIMIT_RSS
		limit(RLIMIT_RSS, limitr);
#else
		if (verbose)
			fprintf(stderr, "system does not support RLIMIT_RSS\n");
#endif
	}
	if (limitt >= -1) {
#ifdef RLIMIT_CPU
		limit(RLIMIT_CPU, limitt);
#else
		if (verbose)
			fprintf(stderr, "system does not support RLIMIT_CPU\n");
#endif
	}

	if (lock) {
		if ((lockfd = open(lock, O_WRONLY | O_APPEND | O_CREAT, 0644)) == -1) {
			perror("unable to open lock");
			exit(1);
		}
		if (flock(lockfd, lockflags) == -1) {
			perror("unable to lock");
			exit(1);
		}
	}

	if (clearenviron) {
		clearenv();
	}

	for (int i = 0; i < envdirpath_len; i++) {
		parse_envdir(envdirpath[i]);
	}

	for (int i = 0; i < envfilepath_len; i++) {
		parse_envfile(envfilepath[i]);
	}

	for (int i = 0; i < 10; i++) {
		if (closefd[i] && close(i) == -1) {
			perror("unable to close stdin");
			exit(1);
		}
	}
	exec = argv[0];
	if (arg0)
		argv[0] = arg0;

	execvpe(exec, argv, environ);
	perror("execute");
	return 127;
}
