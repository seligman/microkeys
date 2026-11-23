#!/usr/bin/env python3

# GUI tests for MicroKeys.  These need a real, unlocked Windows desktop: they
# start MicroKeys with a test script, press real (synthesized) keys into a
# test window, and check what arrives.  Run them from the console session,
# not over SSH.
#
# Usage: run_gui_tests.py --exe <path to MicroKeysApp.exe> [--work <dir>]

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import shutil
import subprocess
import sys
import textwrap
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 2
VK = {"ctrl": 0x11, "alt": 0x12, "shift": 0x10, "back": 0x08, "delete": 0x2E, "enter": 0x0D, "f7": 0x76,
      "f8": 0x77, "f9": 0x78, "f10": 0x79, "f11": 0x7A, "f12": 0x7B, "a": 0x41, "b": 0x42, "i": 0x49, "j": 0x4A,
      "k": 0x4B, "l": 0x4C, "m": 0x4D, "r": 0x52, "u": 0x55}
WM_COMMAND = 0x0111
WM_SETTEXT = 0x000C
WM_KEYDOWN = 0x0100
WM_CLOSE = 0x0010
WM_APP_SHOW = 0x8000 + 9
GWL_EXSTYLE = -20
WS_EX_TOPMOST, WS_EX_TRANSPARENT, WS_EX_NOACTIVATE = 0x8, 0x20, 0x08000000
EM_GETLINECOUNT = 0x00BA
EM_GETFIRSTVISIBLELINE = 0x00CE
VK_RETURN = 0x0D
IDM_EXIT = 105
ID_RELOAD = 32771
ID_PAUSE = 32783
CF_UNICODETEXT = 13
MOD_ALT, MOD_CONTROL = 1, 2

TARGET = "MicroKeys Test Target"
OTHER = "Other Target"
LATER = "Opened Later"

HERE = os.path.dirname(os.path.abspath(__file__))


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wt.DWORD), ("u", _INPUTUNION)]


def key_event(vk, up=False):
    inp = INPUT(type=INPUT_KEYBOARD)
    inp.u.ki = KEYBDINPUT(wVk=vk, wScan=0, dwFlags=KEYEVENTF_KEYUP if up else 0, time=0, dwExtraInfo=0)
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))
    time.sleep(0.02)


def combo(*names):
    # Press keys like a person would: down in order, up in reverse
    vks = [VK[n] for n in names]
    for vk in vks:
        key_event(vk)
    for vk in reversed(vks):
        key_event(vk, up=True)
    time.sleep(0.05)


def type_keys(text):
    # Type with real key presses (not as Unicode), so the hotstring hook sees
    # the same thing as from a keyboard
    for ch in text:
        res = user32.VkKeyScanW(ord(ch))
        vk, shift = res & 0xFF, (res >> 8) & 1
        if shift:
            key_event(VK["shift"])
        key_event(vk)
        key_event(vk, up=True)
        if shift:
            key_event(VK["shift"], up=True)


def find_window(title, cls=None):
    return user32.FindWindowW(cls, title)


def set_clipboard(text):
    # As another program would: MicroKeys doesn't own the clipboard after this
    kernel32.GlobalAlloc.restype = ctypes.c_void_p
    kernel32.GlobalLock.restype = ctypes.c_void_p
    kernel32.GlobalLock.argtypes = [ctypes.c_void_p]
    kernel32.GlobalUnlock.argtypes = [ctypes.c_void_p]
    user32.SetClipboardData.argtypes = [ctypes.c_uint, ctypes.c_void_p]
    data = (text + "\0").encode("utf-16-le")
    mem = kernel32.GlobalAlloc(2, len(data))
    ctypes.memmove(kernel32.GlobalLock(mem), data, len(data))
    kernel32.GlobalUnlock(mem)
    for attempt in range(20):
        if user32.OpenClipboard(None):
            user32.EmptyClipboard()
            user32.SetClipboardData(CF_UNICODETEXT, mem)
            user32.CloseClipboard()
            return
        time.sleep(0.05)
    raise Exception("Unable to open the clipboard")


def focus(title):
    hwnd = find_window(title)
    if not hwnd:
        raise Exception(f"No window titled {title!r}")
    # An Alt key-up lets this process take the foreground
    key_event(VK["alt"], up=True)
    user32.ShowWindow(hwnd, 9)
    user32.SetForegroundWindow(hwnd)
    wait_for(lambda: user32.GetForegroundWindow() == hwnd, 3, f"{title} in front")
    time.sleep(0.2)
    return hwnd


def wait_for(cond, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    raise Exception(f"Timed out waiting for {what}")


class Run:
    def __init__(self, exe, work):
        self.exe = exe
        self.work = work
        self.scripts = os.path.join(work, "scripts")
        self.data = os.path.join(work, "data")
        self.results = os.path.join(work, "results.jsonl")
        self.target_out = os.path.join(work, "target.txt")
        self.other_out = os.path.join(work, "other.txt")
        self.log = os.path.join(self.data, "MicroKeys.log")
        self.proc = None
        self.targets = []
        self.passed = []
        self.failed = []

    def records(self, name=None):
        if not os.path.isfile(self.results):
            return []
        with open(self.results, encoding="utf-8") as f:
            rows = [json.loads(line) for line in f if line.strip()]
        return [r for r in rows if name is None or r[0] == name]

    def wait_record(self, name, count=1, timeout=10):
        wait_for(lambda: len(self.records(name)) >= count, timeout, f"record {name}")
        return self.records(name)[count - 1][1]

    def log_text(self):
        try:
            with open(self.log, encoding="utf-8", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def target_text(self, out=None):
        try:
            with open(out or self.target_out, encoding="utf-8") as f:
                return f.read()
        except OSError:
            return ""

    def clear_target(self, title=TARGET):
        focus(title)
        combo("ctrl", "a")
        combo("delete")
        out = self.target_out if title == TARGET else self.other_out
        wait_for(lambda: self.target_text(out) == "", 3, "target cleared")

    def check(self, name, func):
        print(f"{name} ... ", end="", flush=True)
        try:
            func()
            print("pass")
            self.passed.append(name)
        except Exception as e:
            print(f"FAIL: {e}")
            self.failed.append(name)

    def write_script(self, name, text):
        # Save through a temp file and a rename, the way many editors do
        tmp = os.path.join(self.scripts, name + ".tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            f.write(textwrap.dedent(text))
        os.replace(tmp, os.path.join(self.scripts, name))

    def setup(self):
        if os.path.isdir(self.work):
            shutil.rmtree(self.work)
        os.makedirs(self.scripts)
        os.makedirs(self.data)
        self.write_script("_record.py", RECORD_HELPER)
        self.write_script("main.py", MAIN_SCRIPT)
        self.write_script("reload_test.py", RELOAD_V1)
        for title, out in ((TARGET, self.target_out), (OTHER, self.other_out)):
            self.targets.append(subprocess.Popen([sys.executable, os.path.join(HERE, "target.py"), title, out]))
        wait_for(lambda: find_window(TARGET) and find_window(OTHER), 10, "test windows")
        # Taken by "another program" before MicroKeys starts
        if not user32.RegisterHotKey(None, 1, MOD_CONTROL | MOD_ALT, VK["f7"]):
            raise Exception("Unable to take Ctrl+Alt+F7 for the conflict test")
        env = dict(os.environ, MK_GUI_RESULTS=self.results)
        self.proc = subprocess.Popen([self.exe, "--scripts", self.scripts, "--data", self.data], env=env)
        wait_for(lambda: "Loaded 2 scripts" in self.log_text(), 20, "MicroKeys to load the scripts")

    def open_later(self):
        self.targets.append(subprocess.Popen([sys.executable, os.path.join(HERE, "target.py"), LATER, os.path.join(self.work, "later.txt")]))

    def teardown(self):
        user32.UnregisterHotKey(None, 1)
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
        for t in self.targets:
            t.kill()

    def main_window(self):
        return find_window(None, "MicroKeysMainWindow")


RECORD_HELPER = '''
    import json, os

    def record(name, value):
        with open(os.environ["MK_GUI_RESULTS"], "a", encoding="utf-8") as f:
            f.write(json.dumps([name, value]) + "\\n")
'''

MAIN_SCRIPT = r'''
    import time
    from microkeys import hotkey, hotstring, command, keys, clip, ui, windows, mouse, every, on_start, unbind
    from _record import record

    TARGET = windows.match(title="MicroKeys Test Target")

    @hotkey("ctrl+alt+f9")
    def t1():
        keys.type("Hello é — \U0001F600\nline2")
        record("t1", "done")

    @hotkey("ctrl+alt+f10")
    def t2():
        keys.press("shift+a")
        keys.type("bc")
        keys.press("left", "left", "delete")
        record("t2", "done")

    hotstring(";sig", "Best regards")
    hotstring("btw", "by the way", immediate=False)

    @hotkey("f8", when=TARGET)
    def t5():
        keys.type("[F8]")
        record("t5", "fired")

    @hotkey("ctrl+alt+f11")
    def t6():
        text = clip.copy_selection()
        record("t6", text)
        keys.type("=" + str(eval(text)))

    @hotkey("ctrl+alt+f12")
    def t7():
        record("t7", "start")
        time.sleep(30)
        record("t7", "end")

    @hotkey("ctrl+alt+i")
    def t10():
        record("t10", ui.input("Name?"))

    @hotkey("ctrl+alt+l")
    def t12():
        record("t12", "start")
        keys.type("x" * 300, delay=0.02)
        record("t12", "end")

    @hotkey("ctrl+alt+m")
    def t14():
        start = time.perf_counter()
        mouse.click()
        record("t14", round((time.perf_counter() - start) * 1000))

    @hotkey("ctrl+alt+f7")
    def t11():
        record("t11", "fired")

    @command("hello-cmd")
    def hello():
        record("t15", "ran")

    @on_start
    def t16():
        record("t16", "started")

    ticks = []

    @every(0.4)
    def t17():
        ticks.append(time.monotonic())
        record("t17", len(ticks))
        if len(ticks) == 3:
            unbind(t17)

    @windows.on_activate(when=windows.match(title="Other Target"))
    def t18(win):
        record("t18", win.title)

    @windows.on_open(when=windows.match(title="Opened Later"))
    def t19(win):
        record("t19", win.title)

    @clip.on_change
    def t21():
        record("t21", clip.get())

    @hotkey("ctrl+alt+j")
    def t22():
        clip.set("set by MicroKeys")
        hotkey("ctrl+alt+k", lambda: record("t22", "bound by a macro"))
        record("t22", "binding")

    @hotkey("ctrl+alt+u")
    def t23():
        record("t23", unbind(hotkey="ctrl+alt+k"))

    @command("many-lines")
    def t25():
        for i in range(300):
            print("line", i)
        record("t25", "printed")

    @hotkey("ctrl+alt+b")
    def t26():
        ui.show("Working on it")
        time.sleep(1.5)
        keys.type("typed under the message")
        ui.show("Done")
        record("t26", "done")

    @command("short-message")
    def t27():
        ui.show("Short", seconds=0.5)
        time.sleep(2)
        record("t27", "done")

    @command("many-messages")
    def t28():
        for i in range(300):
            ui.show(f"Step {i} of 300\n" + "word " * (i % 40))
        ui.hide()
        record("t28", "done")
'''

RELOAD_V1 = '''
    from microkeys import hotkey
    from _record import record

    @hotkey("ctrl+alt+r")
    def t8():
        record("t8", "v1")
'''


def run_tests(r):
    def t_conflict():
        text = r.log_text()
        assert "Unable to register Ctrl+Alt+F7: another program is already using it" in text, "no conflict message in the log"

    def t_type_unicode():
        r.clear_target()
        combo("ctrl", "alt", "f9")
        r.wait_record("t1")
        wait_for(lambda: r.target_text() == "Hello é — \U0001F600\nline2", 5, f"typed text, have {r.target_text()!r}")

    def t_press():
        r.clear_target()
        combo("ctrl", "alt", "f10")
        r.wait_record("t2")
        wait_for(lambda: r.target_text() == "Ac", 5, f"'Ac', have {r.target_text()!r}")

    def t_hotstring():
        r.clear_target()
        type_keys(";sig")
        wait_for(lambda: r.target_text() == "Best regards", 5, f"replacement, have {r.target_text()!r}")

    def t_hotstring_end():
        r.clear_target()
        type_keys("say btw ")
        wait_for(lambda: r.target_text() == "say by the way ", 5, f"replacement, have {r.target_text()!r}")
        type_keys("abtw ")
        time.sleep(0.5)
        assert r.target_text() == "say by the way abtw ", f"fired inside a word: {r.target_text()!r}"

    def t_per_app():
        r.clear_target()
        # Per-app hotkeys follow the foreground window a moment behind
        time.sleep(0.5)
        combo("f8")
        r.wait_record("t5")
        wait_for(lambda: r.target_text() == "[F8]", 5, f"[F8], have {r.target_text()!r}")
        r.clear_target(OTHER)
        time.sleep(0.5)
        combo("f8")
        time.sleep(1)
        assert len(r.records("t5")) == 1, "F8 fired outside the target"
        assert r.target_text(r.other_out) == "", "something typed into the other window"

    def t_copy_selection():
        r.clear_target()
        type_keys("2*21")
        combo("ctrl", "a")
        combo("ctrl", "alt", "f11")
        # Selecting all in a Tk text box takes its trailing newline too
        assert r.wait_record("t6").strip() == "2*21"
        wait_for(lambda: r.target_text() == "=42", 5, f"'=42', have {r.target_text()!r}")

    def t_mouse_timing():
        hwnd = focus(TARGET)
        rect = wt.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        user32.SetCursorPos((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2)
        combo("ctrl", "alt", "m")
        ms = r.wait_record("t14")
        assert ms < 200, f"mouse.click() took {ms} ms"

    def t_dialog():
        combo("ctrl", "alt", "i")
        wait_for(lambda: find_window("MicroKeys", "#32770"), 5, "the input dialog")
        time.sleep(0.3)
        type_keys("Zed")
        combo("enter")
        assert r.wait_record("t10") == "Zed"

    def t_abort():
        combo("ctrl", "alt", "f12")
        r.wait_record("t7")
        time.sleep(0.5)
        combo("ctrl", "alt", "back")
        wait_for(lambda: "Stopped t7 (Ctrl+Alt+F12)" in r.log_text(), 5, "the macro to stop")
        time.sleep(0.5)
        assert len(r.records("t7")) == 1, "the macro kept running"
        assert "Internal error" not in r.log_text(), "stopping logged an internal error"

    def t_reload_rename_save():
        combo("ctrl", "alt", "r")
        assert r.wait_record("t8") == "v1"
        r.write_script("reload_test.py", RELOAD_V1.replace('"v1"', '"v2"'))
        wait_for(lambda: r.log_text().count("Scripts changed, reloading") >= 1, 5, "a reload")
        time.sleep(1)
        combo("ctrl", "alt", "r")
        assert r.wait_record("t8", 2) == "v2"
        # The script isn't held open either
        os.unlink(os.path.join(r.scripts, "reload_test.py"))
        r.write_script("reload_test.py", RELOAD_V1)

    def t_reload_during_macro():
        r.clear_target()
        count = len(r.records("t12"))
        combo("ctrl", "alt", "l")
        r.wait_record("t12", count + 1)
        time.sleep(1)
        user32.PostMessageW(r.main_window(), WM_COMMAND, ID_RELOAD, 0)
        r.wait_record("t12", count + 2, timeout=20)
        assert r.records("t12")[-1][1] == "end"
        assert r.proc.poll() is None, "MicroKeys exited"

    def t_command_forwarding():
        subprocess.run([r.exe, "run", "hello-cmd"], timeout=10)
        assert r.wait_record("t15") == "ran"

    def t_exit_during_macro():
        r.clear_target()
        count = len(r.records("t12"))
        combo("ctrl", "alt", "l")
        r.wait_record("t12", count + 1)
        time.sleep(1)
        user32.PostMessageW(r.main_window(), WM_COMMAND, IDM_EXIT, 0)
        try:
            r.proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            raise Exception("MicroKeys was still running 8 seconds after Exit")
        assert "Internal error" not in r.log_text(), "stopping logged an internal error"

    def t_on_start():
        assert r.wait_record("t16") == "started"

    def t_timer():
        wait_for(lambda: len(r.records("t17")) >= 3, 5, "three timer runs")
        time.sleep(1.2)
        assert len(r.records("t17")) == 3, "the timer kept running after unbind()"

    def t_on_activate():
        focus(TARGET)
        count = len(r.records("t18"))
        focus(OTHER)
        assert r.wait_record("t18", count + 1) == OTHER
        focus(TARGET)
        time.sleep(0.5)
        assert len(r.records("t18")) == count + 1, "on_activate ran for another window"

    def t_on_open():
        r.open_later()
        assert r.wait_record("t19") == LATER

    def t_clipboard_event():
        count = len(r.records("t21"))
        set_clipboard("from another program")
        wait_for(lambda: len(r.records("t21")) > count, 5, "the clipboard handler")
        assert r.records("t21")[-1][1] == "from another program"

    def t_runtime_binding():
        count = len(r.records("t21"))
        combo("ctrl", "alt", "j")
        r.wait_record("t22")
        time.sleep(0.5)
        # MicroKeys' own clip.set() isn't a change worth reporting
        assert all(v != "set by MicroKeys" for k, v in r.records("t21")[count:]), "clip.set() ran the clipboard handler"
        combo("ctrl", "alt", "k")
        assert r.wait_record("t22", 2) == "bound by a macro"
        combo("ctrl", "alt", "u")
        assert r.wait_record("t23") == 1
        time.sleep(0.5)
        combo("ctrl", "alt", "k")
        time.sleep(1)
        assert len(r.records("t22")) == 2, "the unbound hotkey still ran"

    def t_pause():
        main = r.main_window()
        user32.PostMessageW(main, WM_COMMAND, ID_PAUSE, 0)
        wait_for(lambda: "Paused:" in r.log_text(), 5, "pause")
        count = len(r.records("t21"))
        set_clipboard("while paused")
        time.sleep(1.5)
        assert len(r.records("t21")) == count, "the clipboard handler ran while paused"
        user32.PostMessageW(main, WM_COMMAND, ID_PAUSE, 0)
        wait_for(lambda: "Resumed" in r.log_text(), 5, "resume")
        set_clipboard("after the pause")
        wait_for(lambda: len(r.records("t21")) > count, 5, "the clipboard handler after resuming")

    def t_console():
        edit = user32.FindWindowExW(r.main_window(), None, "Edit", None)
        assert edit, "no console input in the log window"
        user32.SendMessageW(edit, WM_SETTEXT, 0, "import _record; _record.record('t20', 6 * 7)")
        user32.PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0)
        assert r.wait_record("t20") == 42
        assert ">>> import _record" in r.log_text()

    def t_log_follows():
        # The log window keeps the newest lines in view
        main = r.main_window()
        user32.PostMessageW(main, WM_APP_SHOW, 0, 0)
        wait_for(lambda: user32.IsWindowVisible(main), 5, "the log window")
        log = user32.FindWindowExW(main, None, "RICHEDIT50W", None)
        assert log, "no log in the log window"
        subprocess.run([r.exe, "run", "many-lines"], timeout=10)
        r.wait_record("t25")
        time.sleep(1)
        lines = user32.SendMessageW(log, EM_GETLINECOUNT, 0, 0)
        first = user32.SendMessageW(log, EM_GETFIRSTVISIBLELINE, 0, 0)
        user32.PostMessageW(main, WM_CLOSE, 0, 0)
        assert lines > 300 and first > lines - 80, f"showing line {first} of {lines}"

    def overlay():
        hwnd = find_window(None, "MicroKeysOverlay")
        return hwnd if hwnd and user32.IsWindowVisible(hwnd) else None

    def t_message():
        target = focus(TARGET)
        r.clear_target()
        combo("ctrl", "alt", "b")
        wait_for(overlay, 3, "the message")
        hwnd = overlay()
        assert user32.GetForegroundWindow() == target, "the message took the focus"
        style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
        want = WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE
        assert style & want == want, f"extended style {style:#x}"
        rect = wt.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        middle = wt.POINT((rect.left + rect.right) // 2, (rect.top + rect.bottom) // 2)
        assert user32.WindowFromPoint(middle) != hwnd, "clicks on the message would stop there"
        r.wait_record("t26")
        done = time.time()
        wait_for(lambda: r.target_text() == "typed under the message", 5, f"typed text, have {r.target_text()!r}")
        wait_for(lambda: not overlay(), 5, "the message to go")
        assert time.time() - done > 1.5, "the message went as soon as the macro finished"

    def t_message_seconds():
        subprocess.run([r.exe, "run", "short-message"], timeout=10)
        wait_for(overlay, 3, "the message")
        wait_for(lambda: not overlay(), 3, "the message to go")
        assert not r.records("t27"), "the message stayed up until the macro finished"
        r.wait_record("t27")

    def t_message_updates():
        # Changing the text while it's up doesn't leak GDI objects
        kernel32.OpenProcess.restype = wt.HANDLE
        kernel32.CloseHandle.argtypes = [wt.HANDLE]
        user32.GetGuiResources.argtypes = [wt.HANDLE, wt.DWORD]
        process = kernel32.OpenProcess(0x1000, False, r.proc.pid)
        try:
            subprocess.run([r.exe, "run", "many-messages"], timeout=10)
            r.wait_record("t28")
            time.sleep(0.5)
            before = user32.GetGuiResources(process, 0)
            subprocess.run([r.exe, "run", "many-messages"], timeout=10)
            r.wait_record("t28", 2)
            time.sleep(0.5)
            grew = user32.GetGuiResources(process, 0) - before
        finally:
            kernel32.CloseHandle(process)
        assert grew <= 2, f"GDI objects grew by {grew}"

    for name, func in [
        ("hotkey taken by another program is reported", t_conflict),
        ("on_start", t_on_start),
        ("timer runs and stops", t_timer),
        ("hotkey types Unicode text", t_type_unicode),
        ("key combinations", t_press),
        ("immediate hotstring", t_hotstring),
        ("hotstring with an ending character", t_hotstring_end),
        ("per-app hotkey", t_per_app),
        ("copy_selection", t_copy_selection),
        ("mouse click timing", t_mouse_timing),
        ("input dialog", t_dialog),
        ("abort hotkey", t_abort),
        ("reload after a rename-style save", t_reload_rename_save),
        ("reload during a macro", t_reload_during_macro),
        ("run a command from another process", t_command_forwarding),
        ("on_activate", t_on_activate),
        ("on_open", t_on_open),
        ("clipboard event", t_clipboard_event),
        ("hotkey bound and unbound by a macro", t_runtime_binding),
        ("pause stops events", t_pause),
        ("console", t_console),
        ("log window follows new lines", t_log_follows),
        ("message on screen", t_message),
        ("message for a set time", t_message_seconds),
        ("message updates don't leak", t_message_updates),
        ("exit during a macro", t_exit_during_macro),
    ]:
        r.check(name, func)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--work", default=os.path.join(os.environ.get("TEMP", "."), "mk_gui_test"))
    args = parser.parse_args()
    r = Run(os.path.abspath(args.exe), args.work)
    try:
        r.setup()
        run_tests(r)
    finally:
        r.teardown()
    print(f"\n{len(r.passed)} passed, {len(r.failed)} failed")
    if r.failed:
        print("Failed: " + ", ".join(r.failed))
        print(f"MicroKeys log: {r.log}")
    return 1 if r.failed else 0


if __name__ == "__main__":
    sys.exit(main())
