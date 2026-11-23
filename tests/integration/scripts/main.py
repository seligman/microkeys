# Commands run by run_integration.py against the real MicroKeysApp.exe in
# headless mode.  Each prints lines the runner checks for.

import ctypes
import os
import sys

from microkeys import command, clip, keys

# A top-level call: its result must not show up in the log
len("no echo")


@command("info")
def info():
    import csv, datetime, json, sqlite3, ssl, zoneinfo
    print("python", sys.version.split()[0])
    print("path0", sys.path[0] == os.path.dirname(__file__))
    print("argv", sys.argv)
    print("stdlib ok", json.dumps([1]), datetime.date(2026, 1, 2).isoformat(), csv.__name__, sqlite3.sqlite_version != "", ssl.OPENSSL_VERSION != "")


@command("files")
def files():
    import _helper
    print("helper", _helper.VALUE)
    with open(os.path.join(os.path.dirname(__file__), "_helper.py"), encoding="utf-8") as f:
        print("read", len(f.read()) > 0)
    print("listdir", "main.py" in os.listdir(os.path.dirname(__file__)))


@command("unicode")
def unicode():
    print("text café — naïve \U0001F600")


@command("clipboard")
def clipboard():
    text = "café — \U0001F600 " + "x" * 100000
    clip.set(text)
    print("clip roundtrip", clip.get() == text)


@command("bad-key")
def bad_key():
    try:
        keys.press("ctrl+nosuchkey")
    except ValueError as e:
        print("bad key error", e)


@command("error")
def error():
    raise ValueError("expected failure")


@command("big-heap")
def big_heap():
    data = "a" * (50 * 1024 * 1024)
    print("big string", len(data))


@command("reload-leak")
def reload_leak():
    # Reloading many times must not grow memory or handles
    from microkeys import _runtime

    class Counters(ctypes.Structure):
        _fields_ = [("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong), ("PeakWorkingSetSize", ctypes.c_size_t),
                    ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                    ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                    ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]

    def measure():
        process = ctypes.windll.kernel32.GetCurrentProcess()
        handles = ctypes.c_ulong()
        ctypes.windll.kernel32.GetProcessHandleCount(process, ctypes.byref(handles))
        counters = Counters()
        counters.cb = ctypes.sizeof(Counters)
        ctypes.windll.psapi.GetProcessMemoryInfo(process, ctypes.byref(counters), counters.cb)
        return handles.value, counters.PrivateUsage

    for i in range(20):
        _runtime.reload(settings=False)
    handles0, private0 = measure()
    for i in range(500):
        _runtime.reload(settings=False)
    handles1, private1 = measure()
    print("reload handles grew", handles1 - handles0)
    print("reload private MB grew", round((private1 - private0) / 1048576, 1))


@command("empty-clipboard")
def empty_clipboard():
    # The old clip.copy() crashed when the clipboard held no text
    user32 = ctypes.windll.user32
    if user32.OpenClipboard(None):
        user32.EmptyClipboard()
        user32.CloseClipboard()
    print("empty clipboard gives", repr(clip.get()))
