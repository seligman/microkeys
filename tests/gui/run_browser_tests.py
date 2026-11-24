#!/usr/bin/env python3

# Finding and clicking text in real web browsers, through accessibility and
# with OCR (screen.find_text).  This isn't part of the regular GUI tests: it
# needs Chrome and Firefox installed, and opens each one with a fresh profile.
# Like run_gui_tests.py, it needs a real, unlocked Windows desktop, so run it
# from the console session, not over SSH.
#
# Usage: run_browser_tests.py --exe <path to MicroKeysApp.exe> [--browsers chrome,firefox] [--work <dir>]
#
# To install the browsers:
#   winget install -e --id Google.Chrome
#   winget install -e --id Mozilla.Firefox

import argparse
import ctypes
import ctypes.wintypes as wt
import functools
import http.server
import json
import os
import shutil
import statistics
import subprocess
import sys
import textwrap
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from run_gui_tests import VK, key_event, user32, wait_for

TITLE = "MicroKeys Browser Test"
METHODS = ["controls", "ocr", "auto"]

BROWSERS = {
    "chrome": {
        "paths": [r"%ProgramFiles%\Google\Chrome\Application\chrome.exe", r"%ProgramFiles(x86)%\Google\Chrome\Application\chrome.exe",
                  r"%LOCALAPPDATA%\Google\Chrome\Application\chrome.exe"],
        "args": lambda profile, url: ["--user-data-dir=" + profile, "--no-first-run", "--no-default-browser-check",
                                      "--disable-search-engine-choice-screen", "--disable-features=Translate,PrivacySandboxSettings4",
                                      "--start-maximized", "--new-window", url],
    },
    "firefox": {
        "paths": [r"%ProgramFiles%\Mozilla Firefox\firefox.exe", r"%ProgramFiles(x86)%\Mozilla Firefox\firefox.exe"],
        "args": lambda profile, url: ["-profile", profile, "-no-remote", "-new-instance", url],
    },
}

# Quiet first runs: no welcome pages, default browser questions or notices
FIREFOX_PREFS = {
    "browser.shell.checkDefaultBrowser": False,
    "browser.startup.homepage_override.mstone": "ignore",
    "startup.homepage_welcome_url": "",
    "startup.homepage_welcome_url.additional": "",
    "browser.aboutwelcome.enabled": False,
    "datareporting.policy.dataSubmissionEnabled": False,
    "datareporting.policy.dataSubmissionPolicyBypassNotification": True,
    "datareporting.policy.firstRunURL": "",
    "toolkit.telemetry.reportingpolicy.firstRun": False,
    "browser.translations.automaticallyPopup": False,
    "browser.tabs.warnOnClose": False,
}

# What to look for, where a click on it should land ("<target>/<word>"), the
# words the pointer may be on, and how it should be found.  Text drawn on a
# canvas is only pixels, so accessibility can't see it; "where" is only part
# of a word on the page; the last paragraph is far below what's shown.
CASES = [
    {"name": "link", "text": "Open the report", "lands": "link", "words": ["Open", "the", "report"]},
    {"name": "button", "text": "Save changes", "lands": "button"},
    {"name": "input", "text": "Typed into a box", "lands": "input"},
    {"name": "phrase", "text": "lazy dog", "lands": "para", "words": ["lazy", "dog"]},
    {"name": "split", "text": "click here to continue", "lands": "split", "words": ["click", "here", "to", "continue"]},
    {"name": "small", "text": "tiny print", "lands": "small", "words": ["tiny", "print"]},
    {"name": "dark", "text": "Night mode", "lands": "dark", "words": ["Night", "mode"]},
    {"name": "canvas", "text": "Painted words", "lands": "canvas", "words": ["text"], "not_by": ["controls"]},
    {"name": "frame", "text": "Text inside a frame", "lands": "frame", "words": ["P"]},
    {"name": "part of a word", "text": "where", "not_by": METHODS},
    {"name": "scrolled away", "text": "Far below the fold", "not_by": METHODS},
    {"scroll": True},
    {"name": "scrolled to", "text": "Far below the fold", "lands": "far", "words": ["Far", "below", "the", "fold"]},
]

SCRIPT = r'''
    import json, os, time
    from microkeys import command, keys, mouse, screen, windows

    METHODS = ["controls", "ocr", "auto"]

    def record(row):
        with open(os.environ["MK_RESULTS"], "a", encoding="utf-8") as f:
            f.write(json.dumps(row) + "\n")

    def landed(win, before):
        # What the page says was clicked, once its title changes
        end = time.monotonic() + 3
        while time.monotonic() < end:
            title = win.title or ""
            if title != before and " | " in title:
                return title.split(" | ", 1)[1].rsplit(" #", 1)[0]
            time.sleep(0.05)
        return None

    def diagnose(win, cases):
        # What OCR reads and what accessibility has, for when a find fails
        from microkeys import controls
        from microkeys._native import native
        work = os.path.dirname(os.environ["MK_RESULTS"])
        shot = screen.capture(win)
        shot.save(os.path.join(work, os.environ["MK_BROWSER"] + "-page.png"))
        lines = [[line.text, line.x, line.y, line.width, line.height] for line in shot.read_text()]
        docs = []
        for doc in controls.find_all(role="document", window=win):
            text = doc.text or ""
            docs.append([doc.name, doc.class_name, doc.bounds, len(text), text[:100]])
        raw = {}
        for case in cases:
            if "text" in case:
                raw[case["name"]] = [native.control_text_find(0, win.handle, case["text"], False, True, 10),
                                     native.control_text_find(0, win.handle, case["text"], False, False, 10)]
        record({"diagnostics": {"bounds": win.bounds, "lines": lines, "documents": docs, "raw": raw}})

    @command("browser")
    def browser():
        with open(os.environ["MK_CASES"], encoding="utf-8") as f:
            cases = json.load(f)
        win = windows.find_one(title="MicroKeys Browser Test*")
        if win is None:
            record({"error": "no browser window"})
            return
        win.activate()
        time.sleep(0.5)
        diagnose(win, cases)
        for case in cases:
            if case.get("scroll"):
                # Click somewhere empty first, so the keys go to the page
                x, y, w, h = win.bounds
                mouse.click(x=x + w // 2, y=y + h - 80)
                time.sleep(0.3)
                keys.press("ctrl+end")
                time.sleep(1.5)
                continue
            for method in METHODS:
                before = win.title
                started = time.perf_counter()
                error = None
                try:
                    found = screen.find_text(case["text"], region=win, method=method)
                except Exception as e:
                    found, error = None, f"{type(e).__name__}: {e}"
                ms = round((time.perf_counter() - started) * 1000)
                hit = None
                seen = None
                if found is not None:
                    found.click()
                    hit = landed(win, before)
                elif method not in case.get("not_by", []):
                    # What was on screen, and what OCR made of it
                    shot = screen.capture(win)
                    name = f"{os.environ['MK_BROWSER']}-{case['name'].replace(' ', '_')}-{method}.png"
                    shot.save(os.path.join(os.path.dirname(os.environ["MK_RESULTS"]), name))
                    seen = [line.text for line in shot.read_text()]
                record({"case": case["name"], "method": method, "found": list(found) if found else None, "ms": ms,
                        "hit": hit, "error": error, "seen": seen})
'''


def browser_exe(name):
    for path in BROWSERS[name]["paths"]:
        path = os.path.expandvars(path)
        if os.path.isfile(path):
            return path
    return None


def windows_titled(prefix):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(hwnd, lparam):
        if user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(512)
            user32.GetWindowTextW(hwnd, buf, 512)
            if buf.value.startswith(prefix):
                found.append(hwnd)
        return True

    user32.EnumWindows(each, 0)
    return found


def bring_forward(hwnd):
    key_event(VK["alt"], up=True)
    user32.ShowWindow(hwnd, 3)
    user32.SetForegroundWindow(hwnd)
    wait_for(lambda: user32.GetForegroundWindow() == hwnd, 5, "the browser in front")


def serve(folder):
    # The test page over HTTP on this machine only, as a real site would be
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args):
            pass

    class Server(http.server.ThreadingHTTPServer):
        def handle_error(self, request, client_address):
            # A browser killed with a connection open isn't news
            pass

    handler = functools.partial(Quiet, directory=folder)
    server = Server(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def kill_browser(profile):
    # Everything started with the test's profile.  Killing the tree of the
    # process this started isn't enough: Firefox's first process only
    # launches the browser and leaves.
    pattern = "*" + profile.replace("[", "`[").replace("]", "`]") + "*"
    subprocess.run(["powershell", "-NoProfile", "-Command",
                    f"Get-CimInstance Win32_Process | Where-Object {{ $_.CommandLine -like '{pattern}' }} | "
                    "ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }"], capture_output=True)


def check(case, row):
    # Whether one find did what it should, and if not, why
    if row.get("error"):
        return row["error"]
    if row["method"] in case.get("not_by", []):
        return None if row["found"] is None else f"found it at {row['found']}, but shouldn't have"
    if row["found"] is None:
        return "not found"
    if row["hit"] is None:
        return f"found at {row['found']}, but the click didn't reach the page"
    target, _, word = row["hit"].partition("/")
    if target != case["lands"]:
        return f"the click landed on {row['hit']!r}"
    words = case.get("words")
    if words and word.lower() not in [w.lower() for w in words]:
        return f"the click landed on the word {word!r}"
    return None


def run_browser(name, exe, url, work):
    profile = os.path.join(work, name + "-profile")
    os.makedirs(profile)
    if name == "firefox":
        with open(os.path.join(profile, "user.js"), "w", encoding="utf-8") as f:
            for key, value in FIREFOX_PREFS.items():
                f.write(f"user_pref({json.dumps(key)}, {json.dumps(value)});\n")
    scripts = os.path.join(work, name + "-scripts")
    data = os.path.join(work, name + "-data")
    results = os.path.join(work, name + "-results.jsonl")
    cases_file = os.path.join(work, "cases.json")
    os.makedirs(scripts)
    os.makedirs(data)
    with open(os.path.join(scripts, "browser.py"), "w", encoding="utf-8") as f:
        f.write(textwrap.dedent(SCRIPT))
    with open(os.path.join(data, "settings.json"), "w", encoding="utf-8") as f:
        json.dump({"check_updates": False}, f)
    with open(cases_file, "w", encoding="utf-8") as f:
        json.dump(CASES, f)

    proc = subprocess.Popen([exe] + BROWSERS[name]["args"](profile, url))
    rows = []
    try:
        wait_for(lambda: windows_titled(TITLE), 60, f"{name} to show the test page")
        hwnd = windows_titled(TITLE)[0]
        bring_forward(hwnd)
        # Let the page and the browser's first-run bits settle
        time.sleep(3)
        bring_forward(hwnd)
        env = dict(os.environ, MK_RESULTS=results, MK_CASES=cases_file, MK_BROWSER=name)
        mk = subprocess.run([ARGS.exe, "--headless", "--scripts", scripts, "--data", data, "--run", "browser"], env=env, timeout=600)
        if os.path.isfile(results):
            with open(results, encoding="utf-8") as f:
                rows = [json.loads(line) for line in f if line.strip()]
        if mk.returncode != 0:
            print(f"  MicroKeys exited with {mk.returncode}; log: {os.path.join(data, 'MicroKeys.log')}")
    finally:
        proc.kill()
        kill_browser(profile)
    diagnostics = [r["diagnostics"] for r in rows if "diagnostics" in r]
    if diagnostics:
        with open(os.path.join(work, name + "-diagnostics.json"), "w", encoding="utf-8") as f:
            json.dump(diagnostics[0], f, indent=1)
    return [r for r in rows if "diagnostics" not in r]


def report(name, rows):
    print(f"\n{name}")
    if rows and "error" in rows[0] and "case" not in rows[0]:
        print(f"  FAIL: {rows[0]['error']}")
        return 1
    cases = {c["name"]: c for c in CASES if "name" in c}
    failures = 0
    times = {m: [] for m in METHODS}
    print(f"  {'case':<16}" + "".join(f"{m:<34}" for m in METHODS))
    by_case = {}
    for row in rows:
        by_case.setdefault(row["case"], {})[row["method"]] = row
        times[row["method"]].append(row["ms"])
    for case_name, methods in by_case.items():
        cells = []
        for method in METHODS:
            row = methods.get(method)
            if row is None:
                cells.append("(not run)")
                failures += 1
                continue
            problem = check(cases[case_name], row)
            if problem:
                failures += 1
                cells.append(f"FAIL {problem}"[:32])
            else:
                cells.append(f"ok {row['ms']} ms" + (f" {row['hit']}" if row["hit"] else ""))
        print(f"  {case_name:<16}" + "".join(f"{c:<34}" for c in cells))
    for case_name, methods in by_case.items():
        for method, row in methods.items():
            problem = check(cases[case_name], row)
            if problem:
                print(f"  {case_name} by {method}: {problem}")
                if row.get("seen") is not None:
                    print(f"    OCR read: {' | '.join(row['seen'])}")
    print("  median ms: " + ", ".join(f"{m} {statistics.median(t):.0f}" for m, t in times.items() if t))
    return failures


def main():
    global ARGS
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--browsers", default="chrome,firefox")
    parser.add_argument("--work", default=os.path.join(os.environ.get("TEMP", "."), "mk_browser_test"))
    ARGS = parser.parse_args()
    ARGS.exe = os.path.abspath(ARGS.exe)
    if os.path.isdir(ARGS.work):
        shutil.rmtree(ARGS.work)
    os.makedirs(ARGS.work)
    shutil.copy(os.path.join(HERE, "browser_page.html"), os.path.join(ARGS.work, "page.html"))
    server = serve(ARGS.work)
    url = f"http://127.0.0.1:{server.server_address[1]}/page.html"

    failures = 0
    for name in ARGS.browsers.split(","):
        exe = browser_exe(name)
        if exe is None:
            print(f"\n{name}: not installed, skipped")
            continue
        failures += report(name, run_browser(name, exe, url, ARGS.work))
    server.shutdown()
    print(f"\n{failures} failed (screenshots and diagnostics in {ARGS.work})" if failures else "\nAll passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
