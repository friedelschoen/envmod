---
title: envmod
section: 1
date: July 2025
---

# NAME

*envmod* - runs a program with modified environment

# SYNOPSIS

*envmod* [options] [name=value] prog [arguments...]

*softlimit* [options] prog [arguments...]

*setuidgid* *[:]user[:group]* prog [arguments...]

*envuidgid* *[:]user[:group]* prog [arguments...]

*pgrphack* prog [arguments...]

*envdir* path prog [arguments...]

*setlock* [*-nNxX*] prog [arguments...]

*env* [options] [name=value] prog [arguments...]

*flock* [options] prog [arguments...]

# DESCRIPTION

*envmod* is a re-implementation of runit's *chpst(8)*. It can also be linked to some daemontools utilities to emulate them.

*setuidgid \<user,group>* is the same as *envmod -u \<user,group>*

*envuidgid \<user,group>* is the same as *envmod -U \<user,group>*

*envdir \<path>* is the same as *envmod -e \<path>*

*pgrphack* is the same as *envmod -P*

*softlimit \<options>* is the same as *envmod \<options>* for chosen options.

*setlock*, *env* and *flock* uses some custom options which are described below.

# OPTIONS

## -u *[:]user[:group]*
setuidgid. Set uid and gid to the user's uid and gid, as found in /etc/passwd. If user is followed by a colon and a group, set the gid to group's gid, as found in /etc/group, instead of user's gid. If group consists of a colon-separated list of group names, envmod sets the group ids of all listed groups. If user is prefixed with a colon, the user and all group arguments are interpreted as uid and gids respectivly, and not looked up in the password or group file. All initial supplementary groups are removed.

## -U *[:]user[:group]*
envuidgid. Set the environment variables $UID and $GID to the user's uid and gid, as found in /etc/passwd. If user is followed by a colon and a group, set $GID to the group's gid, as found in /etc/group, instead of user's gid. If user is prefixed with a colon, the user and group arguments are interpreted as uid and gid respectivly, and not looked up in the password or group file.

## -b *argv0*
Run prog with argv0 as the 0th argument.

## -/ *root*
Change the root directory to root before starting prog.

## -C *pwd*
Change the working directory to pwd before starting prog. When combined with -/, the working directory is changed after the chroot.

## -e dir
Set various environment variables as specified by files in the directory dir: If dir contains a file named k whose first line is v, envmod removes the environment variable k if it exists, and then adds the environment variable k with the value v. The name k must not contain =. Spaces and tabs at the end of v are removed, and nulls in v are changed to newlines. If the file k is empty (0 bytes long), envmod removes the environment variable k if it exists, without adding a new variable. Can be used multiple times.

## -E file
Set various environment variables as specified by a file *path*. This file contains lines of variables, key and value are delimited by '='. If the value is empty (has a trailing '='), the key is removed from the environment. Lines without a '=' are ignored. Leading and trailing whitespaces are striped before. Can be used multiple times.

## -F
Fork and redirect incoming signals to the child.

## -i *signal*
Ignore incoming *signal* and don't deliver it to the child. This option enables *-F*.

## -T *signal* *command*
Execute handler *command* before delivering signal to child. The command is executed within a shell (either *$SHELL* or "sh") with environment variable *"signo"* set to the number of the incoming signal and *"signame"* to the identifier of the signal (e.g. *SIGINT*). This option enables *-F*.

## -S
Use a shell (either *$SHELL* or "sh") to execute prog.

## -n *inc*
Add inc to the nice(2) value before starting prog. inc must be an integer, and may start with a minus or plus.

## -l *lock*
Open the file lock for writing, and obtain an exclusive lock on it. lock will be created if it does not exist. If lock is locked by another process, wait until a new lock can be obtained.

## -L *lock*
The same as -l, but fail immediately if lock is locked by another process.

## -x
Clear environment before setting environment-variables.

## -k *variable*
Removes *variable* from the environment. But when used with *-x*, clear the environment but keep *variable*. Can be used multiple times.

## -P
Run prog in a new process group.

## -0
Close standard input before starting prog.

## -1
Close standard output before starting prog.

## -2
Close standard error before starting prog.

## -3, -4, -5, -6, -7, -8, -9
Close file descriptor N before starting prog.

## -v
Print verbose messages to standard error. This includes warnings about limits unsupported by the system.

## SOFTLIMIT
Following options are understood by envmod, but also by *softlimit*.

## -m *bytes*
Limit the data segment, stack segment, locked physical pages, and total of all segment per process to bytes bytes each.

## -d *bytes*
limit data segment. Limit the data segment per process to bytes bytes.

## -o *n*
Limit the number of open file descriptors per process to n.

## -p *n*
Limit the number of processes per uid to n.

## -f *bytes*
Limit the output file size to bytes bytes.

## -c *bytes*
Limit the core file size to bytes bytes.

## -r *n*
Limit the resident set size to n bytes. This limit is not enforced unless physical memory is full.

## -t *n*
Limit the CPU time to n seconds. This limit is not enforced except that the process receives a SIGXCPU signal after n seconds.

## -s *n*
Limit the stack segment per process to n bytes.

## -M *n*
Limit the locked physical pages per process to n bytes. This option has no effect on some operating systems. In *softlimit*, this option is also called *-l*.

## SETLOCK

## -n
Causes *setlock* to exit immediately if the lock is already held by another process.

## -N
Causes *setlock* to wait until the lock is released by another process. This is the default behavior.

## -x, -X
These options modify the exit behavior, this is not supported in *envmod* and is ignored.

## ENV

## -i
Starts with a clean environment.

## -u *variable*
Removes *variable* from the environment. But when used with *-x*, clear the environment but keep *variable*. Can be used multiple times.

## -C *dir*
Changes the directory to *dir*.

## -v
Be talkative about what went wrong.

## FLOCK

## -c
Use a shell (either *$SHELL* or "sh") to execute prog.

## -e, -x
Lock exclusively for *prog*. Mutually exclusive with *-s*.

## -s
Make a shared lock. Mutually exclusive with *-e* and *-x*.

## -n
Don't wait for the lock and exit immediately if locked.

## -o
Unlock and close the file before executing *prog*.

## -w *timeout*
Wait up to *timeout* seconds for locking. Exit if locked. A fractional timeout is allowed.

## -v
Be talkative about what went wrong.

## -E, -F, -u
These options are ignored.

# EXIT STATUS

envmod exits 100 when called with wrong options. It prints an error message and exits 1 if it has trouble changing the process state. Otherwise its exit code is the same as that of prog.

# AUTHOR

Based on the implementation by Gerrit Pape \<pape@smarden.org>,
rewritten by Friedel Schön \<derfriedmundschoen@gmail.com>

# LICENSE

Zlib License
