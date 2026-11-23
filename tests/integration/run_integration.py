#!/usr/bin/env python3

# Integration tests: run the real MicroKeysApp.exe headless against the
# scripts in this folder and check its log.  No desktop needed, so these run
# over SSH and in CI.
#
# Usage: run_integration.py [--exe <MicroKeysApp.exe>]

import argparse
import glob
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
]


def find_exe():
    pattern = os.path.join(ROOT, "build", "x64", "stage", "Release", "MicroKeys", "app-*", "MicroKeysApp.exe")
    found = sorted(glob.glob(pattern))
    if not found:
        raise SystemExit(f"Unable to find {pattern}, pass --exe")
    return found[-1]


def run(exe, scripts, commands, work):
    # One headless run; returns (exit code, log lines without time stamps)
    data = os.path.join(work, "data")
    log = os.path.join(work, "log.txt")
    if os.path.isdir(data):
        shutil.rmtree(data)
    if os.path.isfile(log):
        os.unlink(log)
    proc = subprocess.run([exe, "--headless", "--scripts", scripts, "--data", data, "--log", log, "--run", ",".join(commands)], timeout=300)
    with open(log, encoding="utf-8") as f:
        lines = [re.sub(r"^\d\d:\d\d:\d\d (! )?", "", x.rstrip("\r\n")).strip() for x in f]
    return proc.returncode, lines


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe")
    args = parser.parse_args()
    exe = os.path.abspath(args.exe or find_exe())
    print(f"Testing {exe}")
    failures = []

    with tempfile.TemporaryDirectory() as work:
        code, lines = run(exe, os.path.join(HERE, "scripts"), ["info", "files", "unicode", "clipboard", "bad-key", "error", "big-heap", "reload-leak", "bom", "empty-clipboard"], work)
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
