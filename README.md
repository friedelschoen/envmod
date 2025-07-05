# `envmod` – runs a program with modified environment

**`envmod`** is a tool to launch a program with a controlled execution environment.
It’s a compact, composable alternative to tools like `chpst`, `setuidgid`, `softlimit`, and `setlock`.

## Goals

* Be small and predictable
* Replacement of chpst

## Use-cases

* Controlled execution in scripts
* Minimal containers or service wrappers
* Alternative to sandboxing tools

## Features

* Set UID and GID (by name or number)
* Drop supplementary groups
* Chroot and chdir
* Apply soft `rlimit` constraints (`-m`, `-d`, `-o`, etc.)
* Set `nice` level
* Close file descriptors (0–9)
* Create new session (`setsid`)
* File locking (`setlock` style)
* Arg0 override (`-b`)
* Drop-in compatibility with:

  * `setuidgid`
  * `envuidgid`
  * `softlimit`
  * `setlock`
  * `pgrphack`
  * `chpst`

## Basic Usage

Run a program as user `nobody` with restricted memory:

```sh
envmod -u nobody -m 10240 my-daemon
```

Launch in a chroot, with a new session and a locked file:

```sh
envmod -u www-data -/ /srv/jail -C / -P -L /var/lock/myprog.lock ./server
```

Close stdin, stdout, and stderr:

```sh
envmod -0 -1 -2 ./program
```

## Compatibility

You can symlink `envmod` to run it in compatibility mode:

```sh
ln -s envmod setuidgid
setuidgid user ./program

ln -s envmod softlimit
softlimit -m 4096 ./program
```

## See Also

Check the manual pages (`man envmod`) for full command-line flags and compatibility details.

## License

Zlib license.