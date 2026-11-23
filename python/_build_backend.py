# A build backend (PEP 517 and PEP 660) for the microkeys package, so
# "pip install -e" works without downloading setuptools.  An editable
# install is a .pth file naming this folder, which editors such as VS Code
# follow, unlike the import hooks other backends use.

import base64
import hashlib
import io
import os
import re
import tarfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "microkeys"
SUMMARY = "Editor support for MicroKeys scripts: the microkeys package, running outside MicroKeys"
TAG = "py3-none-any"


def _version():
    # The repo keeps the version in src/version.h; a copy MicroKeys made
    # carries it in VERSION
    fn = os.path.join(HERE, "VERSION")
    if os.path.isfile(fn):
        with open(fn, encoding="utf-8") as f:
            return f.read().strip()
    with open(os.path.join(HERE, "..", "src", "version.h"), encoding="utf-8") as f:
        return re.search(r'VERSION_TEXT\s+"([^"]+)"', f.read()).group(1)


def _dist_info():
    return f"{NAME}-{_version()}.dist-info"


def _metadata():
    return (f"Metadata-Version: 2.1\nName: {NAME}\nVersion: {_version()}\nSummary: {SUMMARY}\n"
            "Requires-Python: >=3.10\n").encode("utf-8")


def _wheel_info():
    return f"Wheel-Version: 1.0\nGenerator: microkeys _build_backend\nRoot-Is-Purelib: true\nTag: {TAG}\n".encode("utf-8")


def _package_files():
    # (path in the wheel, path on disk) for the package and its type stub
    ret = [("_microkeys.pyi", os.path.join(HERE, "_microkeys.pyi"))]
    package = os.path.join(HERE, NAME)
    for name in sorted(os.listdir(package)):
        if name.endswith(".py") or name == "py.typed":
            ret.append((f"{NAME}/{name}", os.path.join(package, name)))
    return ret


def _write_wheel(wheel_directory, files):
    # files: (path in the wheel, bytes).  Adds the dist-info and RECORD.
    fn = f"{NAME}-{_version()}-{TAG}.whl"
    info = _dist_info()
    files = list(files) + [(f"{info}/METADATA", _metadata()), (f"{info}/WHEEL", _wheel_info())]
    record = []
    with zipfile.ZipFile(os.path.join(wheel_directory, fn), "w", zipfile.ZIP_DEFLATED) as zf:
        for path, data in files:
            zf.writestr(path, data)
            digest = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode("ascii")
            record.append(f"{path},sha256={digest},{len(data)}")
        record.append(f"{info}/RECORD,,")
        zf.writestr(f"{info}/RECORD", "\n".join(record) + "\n")
    return fn


def _read(fn):
    with open(fn, "rb") as f:
        return f.read()


def get_requires_for_build_wheel(config_settings=None):
    return []


def get_requires_for_build_editable(config_settings=None):
    return []


def get_requires_for_build_sdist(config_settings=None):
    return []


def build_wheel(wheel_directory, config_settings=None, metadata_directory=None):
    return _write_wheel(wheel_directory, [(path, _read(fn)) for path, fn in _package_files()])


def build_editable(wheel_directory, config_settings=None, metadata_directory=None):
    return _write_wheel(wheel_directory, [(f"{NAME}.pth", (HERE + "\n").encode("utf-8"))])


def build_sdist(sdist_directory, config_settings=None):
    base = f"{NAME}-{_version()}"
    fn = f"{base}.tar.gz"
    files = [(path, fn) for path, fn in _package_files()]
    files += [(name, os.path.join(HERE, name)) for name in ("pyproject.toml", "_build_backend.py")]
    with tarfile.open(os.path.join(sdist_directory, fn), "w:gz") as tf:
        for path, src in files:
            tf.add(src, arcname=f"{base}/{path}")
        # An sdist builds without src/version.h, so it carries the version
        data = _version().encode("utf-8")
        info = tarfile.TarInfo(f"{base}/VERSION")
        info.size = len(data)
        tf.addfile(info, io.BytesIO(data))
        meta = _metadata()
        info = tarfile.TarInfo(f"{base}/PKG-INFO")
        info.size = len(meta)
        tf.addfile(info, io.BytesIO(meta))
    return fn
