#!/usr/bin/env python3

# Integration tests: run the real MicroKeysApp.exe headless against the
# scripts in this folder and check its log.  No desktop needed, so these run
# over SSH and in CI.
#
# Usage: run_integration.py [--exe <MicroKeysApp.exe>] [--network]
#
# --network adds the checks that download from PyPI.

import argparse
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")

# Lines the main run's log must contain (the time stamp is ignored)
EXPECTED = [
    "python 3.",
    "path0 True",
    "argv ['MicroKeys']",
    "stdlib ok [1] 2026-01-02 csv True True",
    "helper from helper",
    "read True",
    "listdir True",
    "text caf\u00e9 \u2014 na\u00efve \U0001F600",
    "clip roundtrip True",
    "bad key error Unknown key name: 'nosuchkey'",
    "ValueError: expected failure",
    "big string 52428800",
    "bom ok",
    "empty clipboard gives ''",
    "on_start ran",
    "html roundtrip True plain ['html', 'text']",
    "rtf roundtrip True",
    "files roundtrip True",
    "image roundtrip True True",
    "dib to png True",
    "save restore True True True",
    "formats ['image', 'text']",
    "python.exe True True",
    "stub files True",
    "message window True",
    "message gdi steady True",
]


def find_exe():
    pattern = os.path.join(ROOT, "build", "x64", "stage", "Release", "MicroKeys", "app-*", "MicroKeysApp.exe")
    found = sorted(glob.glob(pattern))
    if not found:
        raise SystemExit(f"Unable to find {pattern}, pass --exe")
    return found[-1]


def run(exe, scripts, commands, work, keep_data=False):
    # One headless run; returns (exit code, log lines without time stamps)
    data = os.path.join(work, "data")
    log = os.path.join(work, "log.txt")
    if os.path.isdir(data) and not keep_data:
        shutil.rmtree(data)
    if os.path.isfile(log):
        os.unlink(log)
    proc = subprocess.run([exe, "--headless", "--scripts", scripts, "--data", data, "--log", log, "--run", ",".join(commands)], timeout=300)
    with open(log, encoding="utf-8") as f:
        lines = [re.sub(r"^\d\d:\d\d:\d\d (! )?", "", x.rstrip("\r\n")).strip() for x in f]
    return proc.returncode, lines


def check_stub_install(exe, work, failures):
    # The staged package installs with "pip install -e" into a plain Python
    lib = os.path.join(os.path.dirname(exe), "lib")
    venv = os.path.join(work, "venv")
    subprocess.run([sys.executable, "-m", "venv", venv], check=True, timeout=300)
    python = os.path.join(venv, "Scripts", "python.exe")
    proc = subprocess.run([python, "-m", "pip", "install", "--no-index", "-e", lib], capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        failures.append("pip install -e of the staged package failed:\n" + proc.stdout + proc.stderr)
        return
    proc = subprocess.run([python, "-c", "import microkeys; microkeys.keys.type('hi'); print(microkeys.version())"],
                          capture_output=True, text=True, cwd=work, timeout=60)
    version = os.path.basename(os.path.dirname(exe))[len("app-"):]
    if proc.stdout.strip() != version or "[microkeys] type 'hi'" not in proc.stderr:
        failures.append(f"the installed stub package didn't work: {proc.stdout!r} {proc.stderr!r}")


def check_packages(exe, work, failures):
    # pip, run by the python.exe that ships with MicroKeys, installs into the
    # packages folder, and scripts can import from it on the next run
    scripts = os.path.join(work, "pkg_scripts")
    os.makedirs(scripts, exist_ok=True)
    with open(os.path.join(scripts, "pkg.py"), "w", encoding="utf-8") as f:
        f.write("from microkeys import command, _packages, _runtime\n\n"
                "@command('install')\n"
                "def install():\n"
                "    print('install ok', _packages.install(_runtime._config, ['six']))\n\n"
                "@command('use')\n"
                "def use():\n"
                "    import six\n"
                "    print('import ok', six.__name__)\n")
    code, lines = run(exe, scripts, ["install"], work)
    if not any("install ok True" in line for line in lines):
        failures.append("installing a package failed:\n  " + "\n  ".join(lines[-15:]))
        return
    code, lines = run(exe, scripts, ["use"], work, keep_data=True)
    if not any("import ok six" in line for line in lines):
        failures.append("an installed package couldn't be imported:\n  " + "\n  ".join(lines[-10:]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe")
    parser.add_argument("--network", action="store_true", help="also run the checks that download from PyPI")
    args = parser.parse_args()
    exe = os.path.abspath(args.exe or find_exe())
    print(f"Testing {exe}")
    failures = []

    with tempfile.TemporaryDirectory() as work:
        code, lines = run(exe, os.path.join(HERE, "scripts"), ["info", "files", "unicode", "clipboard", "bad-key", "error", "big-heap",
                                                              "reload-leak", "bom", "empty-clipboard", "rich-clipboard", "store", "store",
                                                              "runtime-files", "message"], work)
        text = "\n".join(lines)
        for want in EXPECTED:
            if not any(want in line for line in lines):
                failures.append(f"missing from the log: {want!r}")
        if code != 1:
            failures.append(f"expected exit code 1 (the one failing command), got {code}")
        if 'main.py", line' not in text:
            failures.append("traceback doesn't name the script and line")
        if any(line == "7" for line in lines):
            failures.append("a top-level expression was echoed to the log")
        m = re.search(r"reload handles grew (-?\d+)", text)
        if not m or int(m.group(1)) > 5:
            failures.append(f"handles grew across 500 reloads: {m.group(1) if m else 'no result'}")
        m = re.search(r"reload private MB grew (-?[\d.]+)", text)
        if not m or float(m.group(1)) > 20:
            failures.append(f"memory grew across 500 reloads: {m.group(1) if m else 'no result'} MB")

        try:
            with open(os.path.join(work, "data", "store.json"), encoding="utf-8") as f:
                store = json.load(f)
        except (OSError, ValueError) as e:
            store = str(e)
        if store != {"count": 2, "items": ["x", "x"]}:
            failures.append(f"store.json after two runs of the store command: {store!r}")

        check_stub_install(exe, work, failures)
        if args.network:
            check_packages(exe, work, failures)

        code, lines = run(exe, os.path.join(HERE, "scripts_broken"), ["broken"], work)
        text = "\n".join(lines)
        for want in ["SyntaxError", "zz_broken.py", "No command named 'broken'"]:
            if want not in text:
                failures.append(f"broken script run: missing {want!r}")
        if code != 1:
            failures.append(f"broken script run: expected exit code 1, got {code}")

    if failures:
        print("FAILED:")
        for cur in failures:
            print("  " + cur)
        return 1
    print("All integration checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
