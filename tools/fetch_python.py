#!/usr/bin/env python3

# Download the pinned CPython build MicroKeys embeds, check its hashes, and
# unpack it.  The NuGet package supplies the headers and import libraries,
# the embeddable zip supplies the runtime that ships next to the exe.
#
# Usage: fetch_python.py --arch x64|arm64 --dest <dir>
# Prints the unpacked folder on success.

import argparse
import hashlib
import json
import os
import shutil
import sys
import urllib.request
import zipfile

# Files in the embeddable zip that MicroKeys doesn't ship
SKIP_RUNTIME = {"python.exe", "pythonw.exe", "python.cat"}


def load_pins():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "python_version.json")) as f:
        return json.load(f)


def sha256_file(fn):
    h = hashlib.sha256()
    with open(fn, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, sha256, cache):
    os.makedirs(cache, exist_ok=True)
    fn = os.path.join(cache, url.split("/")[-1])
    if os.path.isfile(fn) and sha256_file(fn) == sha256:
        return fn
    print(f"Downloading {url}", file=sys.stderr)
    tmp = fn + ".tmp"
    with urllib.request.urlopen(url) as resp, open(tmp, "wb") as f:
        shutil.copyfileobj(resp, f)
    got = sha256_file(tmp)
    if got != sha256:
        os.unlink(tmp)
        raise Exception(f"Hash mismatch for {url}: expected {sha256}, got {got}")
    os.replace(tmp, fn)
    return fn


def unpack_sdk(nupkg, dest):
    # Only the headers and import libraries are needed from the NuGet package
    with zipfile.ZipFile(nupkg) as zf:
        for info in zf.infolist():
            for prefix, target in (("tools/include/", "include"), ("tools/libs/", "libs")):
                if info.filename.startswith(prefix) and not info.is_dir():
                    rel = info.filename[len(prefix):]
                    out = os.path.join(dest, target, *rel.split("/"))
                    os.makedirs(os.path.dirname(out), exist_ok=True)
                    with zf.open(info) as src, open(out, "wb") as dst:
                        shutil.copyfileobj(src, dst)


def unpack_runtime(zip_fn, dest):
    os.makedirs(dest, exist_ok=True)
    with zipfile.ZipFile(zip_fn) as zf:
        for info in zf.infolist():
            name = info.filename
            if "/" in name or name in SKIP_RUNTIME or name.endswith("._pth"):
                continue
            with zf.open(info) as src, open(os.path.join(dest, name), "wb") as dst:
                shutil.copyfileobj(src, dst)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", required=True, choices=["x64", "arm64"])
    parser.add_argument("--dest", required=True)
    args = parser.parse_args()

    pins = load_pins()
    ver = pins["version"]
    root = os.path.abspath(os.path.join(args.dest, f"python-{ver}-{args.arch}"))
    marker = os.path.join(root, "ok.txt")
    pin = pins[args.arch]
    stamp = pin["sdk"]["sha256"] + pin["runtime"]["sha256"]

    if os.path.isfile(marker):
        with open(marker) as f:
            if f.read().strip() == stamp:
                print(root)
                return

    if os.path.isdir(root):
        shutil.rmtree(root)
    cache = os.path.join(os.path.abspath(args.dest), "cache")
    unpack_sdk(download(pin["sdk"]["url"], pin["sdk"]["sha256"], cache), os.path.join(root, "sdk"))
    unpack_runtime(download(pin["runtime"]["url"], pin["runtime"]["sha256"], cache), os.path.join(root, "runtime"))
    with open(marker, "w") as f:
        f.write(stamp + "\n")
    print(root)


if __name__ == "__main__":
    main()
