#include "arg.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <unistd.h>


/* uid:gid[:gid[:gid]...] */
static int parse_ugid_num(char *str, uid_t *uid, gid_t *gids) {
	int   i;
	char *end;

	*uid = strtoul(str, &end, 10);

	if (*end != ':')
		return -1;

	str = end + 1;
	for (i = 0; i < 60; ++i, ++str) {
		gids[i++] = strtoul(str, &end, 10);

		if (*end != ':')
			break;

		str = end + 1;
	}

	if (*str != '\0')
		return -1;

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
		if ((gr = getgrnam(groupstr)) == NULL)
			return -1;

		gids[gid_size++] = gr->gr_gid;
	}

	return gid_size;
}

void limit(int what, rlim_t l) {
	struct rlimit r;

	if (getrlimit(what, &r) == -1)
		fprintf(stderr, "error: unable to getrlimit\n");

	if (l < 0) {
		r.rlim_cur = 0;
	} else if (l > r.rlim_max)
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
	                "       pgrphack prog [arguments...]\n");

	exit(code);
}

int main(int argc, char **argv) {
	int   opt, lockfd, lockflags, gid_len = 0;
	char *arg0 = NULL, *root = NULL, *cd = NULL, *lock = NULL, *exec = NULL;
	uid_t uid = 0;
	gid_t gid[61];
	long  limitd = -2, limits = -2, limitl = -2, limita = -2, limito = -2, limitp = -2, limitf = -2, limitc = -2,
	     limitr = -2, limitt = -2;
	long nicelevel = 0;
	int  ssid      = 0;
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
		gid_len = parse_ugid(argv[1], &uid, gid);
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
		lock = self;
		SHIFT;
	} else if (!strcmp(self, "softlimit")) {
		ARGBEGIN
		switch (OPT) {
			case 'm':
				limits = limitl = limita = limitd = atoi(EARGF(usage(1)));
				break;
			case 'a':
				limita = atoi(EARGF(usage(1)));
				break;
			case 'd':
				limitd = atoi(EARGF(usage(1)));
				break;
			case 'o':
				limito = atoi(EARGF(usage(1)));
				break;
			case 'p':
				limitp = atoi(EARGF(usage(1)));
				break;
			case 'f':
				limitf = atoi(EARGF(usage(1)));
				break;
			case 'c':
				limitc = atoi(EARGF(usage(1)));
				break;
			case 'r':
				limitr = atoi(EARGF(usage(1)));
				break;
			case 't':
				limitt = atoi(EARGF(usage(1)));
				break;
			case 'l':
				limitl = atoi(EARGF(usage(1)));
				break;
			case 's':
				limits = atoi(EARGF(usage(1)));
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
			case 'U':
				gid_len = parse_ugid(EARGF(usage(1)), &uid, gid);
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
				nicelevel = atoi(EARGF(usage(1)));
				break;
			case 'l':
				lock      = EARGF(usage(1));
				lockflags = LOCK_EX | LOCK_NB;
				break;
			case 'L':
				lock      = EARGF(usage(1));
				lockflags = LOCK_EX;
				break;
			case 'v':    // ignored
				break;
			case 'P':
				ssid++;
				break;
			case '0' ... '9':
				closefd[OPT - '0'] = 1;
				break;
			case 'm':
				limits = limitl = limita = limitd = atoi(EARGF(usage(1)));
				break;
			case 'd':
				limitd = atoi(EARGF(usage(1)));
				break;
			case 'o':
				limito = atoi(EARGF(usage(1)));
				break;
			case 'p':
				limitp = atoi(EARGF(usage(1)));
				break;
			case 'f':
				limitf = atoi(EARGF(usage(1)));
				break;
			case 'c':
				limitc = atoi(EARGF(usage(1)));
				break;
			case 'r':
				limitr = atoi(EARGF(usage(1)));
				break;
			case 't':
				limitt = atoi(EARGF(usage(1)));
				break;
			case 'e':
				fprintf(stderr, "warning: '-%c' is ignored\n", optopt);
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
		setsid();
	}

	if (uid) {
		setgroups(gid_len, gid);
		setgid(gid[0]);
		setuid(uid);
		// $EUID
	}

	if (root) {
		if (chroot(root) == -1) {
			perror("unable to change root directory");
			exit(1);
		}
		// chdir to '/', otherwise the next command will complain 'directory not found'
		if (chdir(cd) == -1)
			perror("unable to change directory");
	}

	if (cd) {
		if (chdir(cd) == -1) {
			perror("unable to change directory");
			exit(1);
		}
	}

	if (nicelevel != 0) {
		if (nice(nicelevel) == -1) {
			perror("unable to set nice level");
			exit(1);
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
		if ((lockfd = open(lock, O_WRONLY | O_APPEND)) == -1) {
			perror("unable to open lock");
			exit(1);
		}
		if (flock(lockfd, lockflags) == -1) {
			perror("unable to lock");
			exit(1);
		}
	}

	for (int i = 0; i < 10; i++) {
		if (closefd[i] && close(i) == -1) {
			perror("unable to close stdin");
			exit(1);
		}
	}
	exec = self;
	if (arg0)
		self = arg0;

	execvp(exec, argv);
	perror("execute");
	return 127;
}
