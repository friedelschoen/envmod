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

*setlock*, *env*, and *flock* use some custom options, described below.

# OPTIONS

## -u *[:]user[:group]*
Set UID and GID to the user's UID and GID, as found in `/etc/passwd`. If user is followed by a colon and a group, set the GID to the group's GID, as found in `/etc/group`, instead of the user's GID. If the group consists of a colon-separated list of group names, *envmod* sets the group IDs of all listed groups. If the user is prefixed with a colon, the user and all group arguments are interpreted as UID and GIDs respectively, and not looked up in the password or group file. All initial supplementary groups are removed.

## -U *[:]user[:group]*
Set the environment variables `$UID` and `$GID` to the user's UID and GID, as found in `/etc/passwd`. If the user is followed by a colon and a group, set `$GID` to the group's GID instead. If the user is prefixed with a colon, the user and group arguments are interpreted as UID and GID respectively, and not looked up in system databases.

## -b *argv0*
Run *prog* with *argv0* as its `argv[0]`.

## -/ *root*
Change the root directory to *root* before starting *prog*.

## -C *pwd*
Change the working directory to *pwd* before starting *prog*. When combined with `-/`, the working directory is changed after the `chroot`.

## -e *dir*
Set environment variables as specified by files in the directory *dir*: if *dir* contains a file named *k* whose first line contains *v*, the environment value *k* is set with value *v*. The name *k* must not contain `=`. Trailing spaces and tabs in *v* are removed, and null bytes in *v* are replaced with newlines. If the file *k* is empty (0 bytes), *envmod* removes the variable without adding a new one. Can be used multiple times.

## -E *file*
Set environment variables as specified in the file *path*. This file contains lines of `key=value` pairs. If a line ends with `=`, the corresponding variable is removed from the environment. Lines without `=` are ignored. Leading and trailing whitespaces are removed. Can be used multiple times.

## -F
Fork and redirect incoming signals to the child.

## -i *signal*
Ignore *signal* and do not deliver it to the child. This option implies `-F`.

## -T *signal* *command*
Execute handler *command* before delivering the *signal* to the child. The command is executed via a shell (`$SHELL` or `sh`), with the environment variables `signo` (signal number) and `signame` (signal name, e.g. `SIGINT`) set. This option implies `-F` and is **not** mutually exclusive with `-i`.

## -S
Use a shell (`$SHELL` or `sh`) to execute *prog*.

## -n *inc*
Add *inc* to the `nice(2)` value before starting *prog*. *inc* must be an integer, optionally prefixed by `+` or `-`.

## -l *lock*
Open *lock* for writing and obtain an exclusive lock. The file will be created if it does not exist. If already locked by another process, wait until it becomes available.

## -L *lock*
Same as `-l`, but fail immediately if the lock cannot be obtained.

## -x
Clear the environment before setting new variables.

## -k *variable*
Remove *variable* from the environment. When used with `-x`, it preserves *variable* while clearing the rest. Can be used multiple times.

## -P
Run *prog* in a new process group.

## -0
Close standard input before starting *prog*.

## -1
Close standard output before starting *prog*.

## -2
Close standard error before starting *prog*.

## -3, -4, -5, -6, -7, -8, -9
Close file descriptor N before starting *prog*.

## -v
Print verbose messages to standard error. This includes warnings about unsupported limits.

## SOFTLIMIT

The following options are understood by *envmod* and also by *softlimit*.

### -m *bytes*
Limit the data segment, stack segment, locked memory, and total memory per process to *bytes*.

### -d *bytes*
Limit data segment size to *bytes*.

### -o *n*
Limit the number of open file descriptors per process to *n*.

### -p *n*
Limit the number of processes per UID to *n*.

### -f *bytes*
Limit the output file size to *bytes*.

### -c *bytes*
Limit the core dump size to *bytes*.

### -r *bytes*
Limit the resident set size to *bytes*. This limit is only enforced when physical memory is exhausted.

### -t *seconds*
Limit CPU time to *seconds*. A `SIGXCPU` is sent after this time elapses.

### -s *bytes*
Limit the stack segment size to *bytes*.

### -M *bytes*
Limit the amount of locked memory to *bytes*. This may have no effect on some systems. In *softlimit*, this option is also `-l`.

## SETLOCK

### -n
Cause *setlock* to exit immediately if the lock is held by another process.

### -N
Wait until the lock is released. This is the default behaviour.

### -x, -X
These modify exit behaviour. They are ignored in *envmod*.

## ENV

### -i
Start with an empty environment.

### -u *variable*
Remove *variable* from the environment. Used with `-i`, it preserves *variable*. Can be used multiple times.

### -C *dir*
Change directory to *dir*.

### -v
Print verbose messages.

## FLOCK

### -c
Use a shell to execute *prog*.

### -e, -x
Obtain an exclusive lock. Mutually exclusive with `-s`.

### -s
Obtain a shared lock. Mutually exclusive with `-e` and `-x`.

### -n
Do not wait for the lock—exit immediately if it is held.

### -o
Unlock and close the file before executing *prog*.

### -w *timeout*
Wait up to *timeout* seconds for the lock. Fractional time is allowed.

### -v
Print verbose messages.

### -E, -F, -u
These options are ignored.

# EXIT STATUS

* 100 – invalid command
* 101 – runtime failure
* 102 – system failure
* 120 – command terminated (signalled)
* 121 – command terminated (unknown)
* 127 – command not found

# AUTHOR

Based on the implementation by Gerrit Pape <pape@smarden.org>,
rewritten by Friedel Schön <derfriedmundschoen@gmail.com>

# LICENCE

Zlib Licence
