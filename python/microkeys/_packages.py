# Packages from PyPI for scripts to import.  They're installed with pip, run
# by the python.exe that ships next to MicroKeys, into a folder of their own
# for each Python version, and only as ready-built wheels, since there's no
# compiler to build anything else.  Also the debugpy debugger, which comes
# the same way.

import hashlib
import os
import queue
import re
import subprocess
import sys
import threading
import urllib.request

from ._native import native, LOG_INFO, LOG_ERROR

PIP_VERSION = "26.3"
PIP_URL = "https://files.pythonhosted.org/packages/07/25/ee8c38ae72d66b844ce8eb9711ba905dba21ca1a8a0c960057c41cd23337/pip-26.3-py3-none-any.whl"
PIP_SHA256 = "f3b60b20ffaadd4415ed7957212a38ac63fa4c5fa375bd0f4a797777940fad13"

# Run pip from its wheel, with the packages folder on the path so pip sees
# what's already installed there
PIP_MAIN = "import sys; sys.path[0:0] = [{wheel!r}, {site!r}]; sys.argv[0] = 'pip'; import runpy; runpy.run_module('pip', run_name='__main__')"

_failed = set()
_debugging = None


def prefix_dir(local_dir):
    return os.path.join(local_dir, "packages", f"{sys.version_info[0]}.{sys.version_info[1]}")


def site_dir(local_dir):
    # Where pip --prefix puts packages on Windows
    return os.path.join(prefix_dir(local_dir), "Lib", "site-packages")


def activate(local_dir):
    # Make the installed packages importable, .pth files and all
    import site
    path = site_dir(local_dir)
    if os.path.isdir(path) and path not in sys.path:
        site.addsitedir(path)


def name_of(spec):
    # "requests>=2.0" -> "requests", normalized the way PyPI compares names
    m = re.match(r"\s*([A-Za-z0-9][A-Za-z0-9._-]*)", spec)
    return re.sub(r"[-_.]+", "-", m.group(1)).lower() if m else None


def installed(local_dir):
    from importlib import metadata
    path = site_dir(local_dir)
    if not os.path.isdir(path):
        return set()
    return {name_of(d.metadata["Name"] or "") for d in metadata.distributions(path=[path])}


def _sha256(fn):
    h = hashlib.sha256()
    with open(fn, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _pip_wheel(local_dir):
    # pip itself, downloaded once and checked against its pinned hash
    fn = os.path.join(local_dir, "pip", PIP_URL.split("/")[-1])
    if os.path.isfile(fn) and _sha256(fn) == PIP_SHA256:
        return fn
    os.makedirs(os.path.dirname(fn), exist_ok=True)
    native.log(LOG_INFO, f"Downloading pip {PIP_VERSION}")
    tmp = fn + ".tmp"
    with urllib.request.urlopen(PIP_URL, timeout=60) as resp, open(tmp, "wb") as f:
        while True:
            chunk = resp.read(1 << 16)
            if not chunk:
                break
            f.write(chunk)
            native.check_cancel()
    if _sha256(tmp) != PIP_SHA256:
        os.unlink(tmp)
        raise RuntimeError(f"The pip download from {PIP_URL} didn't match its expected hash")
    os.replace(tmp, fn)
    return fn


def pip(config, args):
    # Run pip, copying what it says into the log.  True if it worked.
    python = os.path.join(config["app_dir"], "python.exe")
    if not os.path.isfile(python):
        native.log(LOG_ERROR, f"Unable to run pip: {python} is missing")
        return False
    local_dir = config["local_dir"]
    try:
        wheel = _pip_wheel(local_dir)
    except Exception as e:
        native.log(LOG_ERROR, f"Unable to get pip: {e}")
        return False
    cmd = [python, "-c", PIP_MAIN.format(wheel=wheel, site=site_dir(local_dir))] + args
    env = dict(os.environ, PYTHONIOENCODING="utf-8", PIP_NO_COLOR="1")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, env=env,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    # Lines come through a thread, so waiting for them can be cut short by the
    # abort hotkey
    lines = queue.Queue()

    def read():
        for raw in proc.stdout:
            lines.put(raw.decode("utf-8", "replace").rstrip())
        lines.put(None)

    threading.Thread(target=read, daemon=True).start()
    try:
        while True:
            try:
                line = lines.get_nowait()
            except queue.Empty:
                native.sleep(0.05)
                continue
            if line is None:
                break
            if line:
                native.log(LOG_INFO, "pip: " + line)
        return proc.wait() == 0
    except BaseException:
        proc.kill()
        raise


def install(config, specs, upgrade=False):
    args = ["install", "--disable-pip-version-check", "--no-input", "--progress-bar", "off",
            "--only-binary", ":all:", "--prefix", prefix_dir(config["local_dir"])]
    if upgrade:
        args.append("--upgrade")
    native.log(LOG_INFO, "Installing " + ", ".join(specs))
    ok = pip(config, args + list(specs))
    if ok:
        activate(config["local_dir"])
    return ok


def ensure(config, specs):
    # Install whatever in `specs` isn't there yet.  What failed isn't tried
    # again until MicroKeys restarts, so an offline reload stays quick.
    have = installed(config["local_dir"])
    missing = [s for s in specs if name_of(s) and name_of(s) not in have and s not in _failed]
    if not missing:
        return True
    if install(config, missing):
        return True
    _failed.update(missing)
    native.log(LOG_ERROR, "Unable to install " + ", ".join(missing) + "; see the pip messages above")
    return False


def debug(port):
    # Let a debugger attach.  debugpy can only listen once per process.
    global _debugging
    if not port or _debugging == port:
        return
    if _debugging is not None:
        native.log(LOG_INFO, f"The debugger stays on port {_debugging} until MicroKeys restarts")
        return
    try:
        # The standard library is frozen, which debugpy warns about; scripts
        # aren't, so their breakpoints work
        os.environ.setdefault("PYDEVD_DISABLE_FILE_VALIDATION", "1")
        import debugpy
        # The adapter normally runs as a separate Python process, which would
        # be MicroKeys itself here
        debugpy.listen(("127.0.0.1", int(port)), in_process_debug_adapter=True)
    except Exception as e:
        native.log(LOG_ERROR, f"Unable to start the debugger on port {port}: {e}")
        return
    _debugging = port
    native.log(LOG_INFO, f"Debugger listening on 127.0.0.1:{port}")
