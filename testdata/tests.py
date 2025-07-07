import subprocess
import os
import shutil
import string
import random
import tempfile


def randomword(length):
   letters = string.ascii_letters + string.digits
   return ''.join(random.choice(letters) for i in range(length))

def run(*args, shell=None, envmod=True, root=False, env=None):
    argv = list(args)
    if envmod:
        argv = ["./envmod"] + argv
    if shell is not None:
        argv += [ 'sh', '-c', shell ]
    result = subprocess.run(
        argv,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    text = result.stdout.strip()
    if result.returncode:
        text = f'{result.returncode}!{text}'
    return text

def test_envuidgid_name():
    assert run("-U", "root:root", shell="echo $UID $GID") == "0 0"

def test_envuidgid_num():
    uid = random.randint(0, 999)
    gid = random.randint(0, 999)
    assert run("-U", f":{uid}:{gid}", shell="echo $UID $GID") == f"{uid} {gid}"

def test_argv0():
    arg0 = randomword(16)
    assert run("-b", arg0, shell="echo $0") == arg0

def test_chdir():
    assert run("-C", "testdata", shell="basename $PWD") == "testdata"

def test_nice_inc():
    value = random.randint(0, 10)
    assert run("-n", str(value), "nice") == str(os.nice(0) + value)

def test_lock():
    with tempfile.TemporaryDirectory() as tmpdirname:
        lockfile = tmpdirname + "/lock"
        assert run("-l", lockfile, "flock", "--verbose", "-n", lockfile, "true") == "1!flock: failed to get lock"

def test_flock():
    with tempfile.TemporaryDirectory() as tmpdirname:
        lockfile = tmpdirname + "/lock"
        assert run("flock", lockfile, "./envmod", "-l", lockfile, "true", envmod=False) == "1!unable to lock: Resource temporarily unavailable"

def test_closestdin():
    assert run("-0", "cat") == "1!cat: -: Bad file descriptor\ncat: closing standard input: Bad file descriptor"

def test_closestdout():
    assert run("-1", "echo", "hello") == "1!echo: write error: Bad file descriptor"

def test_closestderr():
    # no output, stderr is closed
    assert run("-2", shell="echo hello 1>&2") == "2!"

def test_closestderr():
    # no output, stderr is closed
    assert run("-2", shell="echo hello 1>&2") == "2!"

def test_setsid():
    assert run("-P", "python3", "-c", "import os; print(os.getsid(0))") != os.getsid(0)

if os.geteuid() == 0:
    def test_nice_dec():
        value = -random.randint(0, 10)
        assert run("-n", str(value), "nice") == str(os.nice(0) + value)

    def test_chroot():
        assert run("-/", 'testdata', "./printhello") == 'hello'
