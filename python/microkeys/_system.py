# log(), sleep(), run(), and open(), exported from the package itself

import subprocess

from ._native import native, LOG_INFO


def log(*values):
    """Write a line to the MicroKeys log, like print()."""
    native.log(LOG_INFO, " ".join(str(v) for v in values))


def sleep(seconds):
    """Pause the macro.  Unlike time.sleep(), the abort hotkey ends it at once."""
    native.sleep(float(seconds))


def run(command, wait=False, cwd=None, hide=False):
    """Start a program.

        run("notepad.exe")
        run(["git", "pull"], wait=True, cwd=r"C:\\src\\project", hide=True)

    `command` is a string or a list of arguments.  With `wait`, waits for it
    to finish and returns its exit code; otherwise returns the process.
    `hide` keeps a console program's window out of sight.
    """
    flags = 0
    if hide and hasattr(subprocess, "CREATE_NO_WINDOW"):
        flags = subprocess.CREATE_NO_WINDOW
    proc = subprocess.Popen(command, cwd=cwd, creationflags=flags)
    if not wait:
        return proc
    while proc.poll() is None:
        native.sleep(0.05)
    return proc.returncode


def open(target):
    """Open a file, folder, or URL with the program the system uses for it."""
    native.shell_open(str(target))


def version():
    """The MicroKeys version, like "1.0.0"."""
    return native.version()
