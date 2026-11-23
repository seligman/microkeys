# Commands run by run_integration.py against the real MicroKeysApp.exe in
# headless mode.  Each prints lines the runner checks for.

import ctypes
import os
import sys

from microkeys import command, clip, keys, on_start

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


@on_start
def started():
    print("on_start ran")


@command("rich-clipboard")
def rich_clipboard():
    import struct
    import zlib

    clip.set("plain", html="<b>bold</b> &amp; more")
    print("html roundtrip", clip.get_html() == "<b>bold</b> &amp; more", clip.get(), sorted(clip.formats()))
    clip.set(rtf=r"{\rtf1\ansi hello}")
    print("rtf roundtrip", clip.get_rtf() == r"{\rtf1\ansi hello}")
    here = os.path.abspath(__file__)
    clip.set(files=[here, os.path.dirname(here)])
    print("files roundtrip", clip.get_files() == [here, os.path.dirname(here)])

    # A 3x2 PNG, red with one see-through pixel, made by hand
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + b"".join(b"\xff\0\0\xff" if (x, y) != (2, 1) else b"\0\0\0\0" for x in range(3)) for y in range(2))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 3, 2, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    clip.set(image=png)
    back = clip.get_image()
    print("image roundtrip", back == png, "image" in clip.formats())

    # Only a DIB on the clipboard (as from an older program): it comes back as PNG
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
    kernel32.GlobalAlloc.restype = ctypes.c_void_p
    kernel32.GlobalLock.restype = ctypes.c_void_p
    kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
    user32.SetClipboardData.argtypes = [ctypes.c_uint, ctypes.c_void_p]
    header = struct.pack("<IiiHHIIiiII", 40, 2, 2, 1, 24, 0, 0, 0, 0, 0, 0)
    pixels = (b"\0\0\xff" * 2 + b"\0\0") * 2
    dib = header + pixels
    mem = kernel32.GlobalAlloc(2, len(dib))
    ctypes.memmove(kernel32.GlobalLock(mem), dib, len(dib))
    kernel32.GlobalUnlock(mem)
    if user32.OpenClipboard(None):
        user32.EmptyClipboard()
        user32.SetClipboardData(8, mem)
        user32.CloseClipboard()
    converted = clip.get_image()
    print("dib to png", converted is not None and converted.startswith(b"\x89PNG") and struct.unpack(">II", converted[16:24]) == (2, 2))

    # Save and restore keeps every format, the image included
    clip.set("caption", html="<i>caption</i>", image=png)
    saved = clip.save()
    clip.set("something else")
    clip.restore(saved)
    print("save restore", clip.get() == "caption", clip.get_html() == "<i>caption</i>", clip.get_image() == png)
    clip.set("caption", image=png)
    print("formats", sorted(clip.formats()))


@command("store")
def store_test():
    from microkeys import store
    store["count"] = store.get("count", 0) + 1
    store.setdefault("items", []).append("x")


@command("message")
def message():
    # Over SSH nothing can be seen, so this checks the window is made and
    # resized without leaking; the GUI tests check what it looks like
    import time
    from microkeys import ui
    user32 = ctypes.windll.user32
    user32.GetGuiResources.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    process = ctypes.c_void_p(-1)
    ui.show("first")
    time.sleep(0.5)
    print("message window", bool(user32.FindWindowW("MicroKeysOverlay", None)))
    before = user32.GetGuiResources(process, 0)
    for i in range(300):
        ui.show(f"step {i}\n" + "word " * (i % 40))
    ui.hide()
    time.sleep(1)
    print("message gdi steady", user32.GetGuiResources(process, 0) - before <= 2)


@command("runtime-files")
def runtime_files():
    app = os.path.dirname(sys.executable)
    print("python.exe", os.path.isfile(os.path.join(app, "python.exe")), os.path.isfile(os.path.join(app, "python._pth")))
    print("stub files", all(os.path.isfile(os.path.join(app, "lib", n)) for n in ("pyproject.toml", "_build_backend.py", "VERSION")))
