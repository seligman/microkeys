# A stand-in for the _microkeys module the app builds in.  It records every
# call and keeps a little simulated desktop (clipboard, windows, pointer), so
# the microkeys package can be tested on any OS without the app.

import types

KNOWN_KEYS = None


class FakeMicrokeys(types.ModuleType):
    def __init__(self):
        super().__init__("_microkeys")
        self.reset()

    def reset(self):
        self.platform_name = "windows"
        self.logs = []
        self.toasts = []
        self.sent = []
        self.clipboard = None
        self.clip_seq = 1
        self.clip_changes_on_copy = True
        self.pointer = (10, 10)
        self.mouse_events = []
        self.bindings = None
        self.config = None
        self.loaded = []
        self.cancel_after_sends = None
        self.slept = 0.0
        self.opened = []
        self.answers = {"msgbox": "ok", "ask_text": "typed text", "ask_choice": 1}
        self.dialogs = []
        self.windows = [
            {"handle": 0x101, "title": "Untitled - Notepad", "class_name": "Notepad", "process": "notepad.exe", "pid": 11},
            {"handle": 0x202, "title": "main.py - Visual Studio Code", "class_name": "Chrome_WidgetWin_1", "process": "Code.exe", "pid": 22},
            {"handle": 0x303, "title": "Calculator", "class_name": "ApplicationFrameWindow", "process": "ApplicationFrameHost.exe", "pid": 33},
        ]
        self.front = 0x101
        self.bounds = {0x101: (100, 100, 800, 600), 0x202: (0, 0, 1920, 1040), 0x303: (50, 50, 300, 500)}
        self.states = {0x101: "normal", 0x202: "maximized", 0x303: "minimized"}
        self.closed = []

    # Helpers for tests
    def log_text(self):
        return "\n".join(text for level, text in self.logs)

    def typed(self):
        # The characters sent with type(), in order
        return "".join(value for kind, value in self.sent if kind == 2)

    def key_events(self):
        return [("down" if kind == 0 else "up", value) for kind, value in self.sent if kind in (0, 1)]

    # General
    def version(self):
        return "1.0.0"

    def platform(self):
        return self.platform_name

    def log(self, level, text):
        self.logs.append((level, text))

    def configure(self, script_dir, abort_mods, abort_key, feed, check):
        self.config = (script_dir, abort_mods, abort_key, feed, check)

    def set_bindings(self, hotkeys, hotstrings, commands):
        self.bindings = (hotkeys, hotstrings, commands)

    def scripts_loaded(self, ok, summary):
        self.loaded.append((ok, summary))

    def macro_running(self, what):
        self.running = what

    def cancelled(self):
        return False

    def check_cancel(self):
        pass

    def sleep(self, seconds):
        self.slept += seconds

    def wait_modifiers(self, timeout):
        return True

    # Keyboard
    def is_key(self, name):
        return name != "f24" or self.platform_name == "windows"

    def send_keys(self, actions, delay):
        for kind, value in actions:
            if kind in (0, 1) and len(value) > 1 and not self.is_key(value):
                raise ValueError(f"Unknown key: {value}")
            if self.cancel_after_sends is not None:
                if self.cancel_after_sends <= 0:
                    raise KeyboardInterrupt
                self.cancel_after_sends -= 1
            self.sent.append((kind, value))
            if kind == 1 and value == "c" and self.clip_changes_on_copy:
                self.clipboard = "selected text"
                self.clip_seq += 1

    # Mouse
    def cursor(self):
        return self.pointer

    def move_cursor(self, x, y):
        self.pointer = (x, y)
        self.mouse_events.append(("move", x, y))

    def mouse_button(self, button, down):
        if button not in ("left", "right", "middle", "x1", "x2"):
            raise ValueError(f"Unknown mouse button: {button}")
        self.mouse_events.append((button, "down" if down else "up"))

    def mouse_wheel(self, clicks, horizontal):
        self.mouse_events.append(("wheel", clicks, horizontal))

    # Clipboard
    def clip_get(self):
        return self.clipboard

    def clip_set(self, text):
        self.clipboard = text
        self.clip_seq += 1
        return True

    def clip_sequence(self):
        return self.clip_seq

    # Windows
    def list_windows(self, include_hidden):
        return [dict(w) for w in self.windows if w["handle"] not in self.closed]

    def foreground(self):
        return self.window_info(self.front)

    def window_info(self, handle):
        for w in self.windows:
            if w["handle"] == handle and handle not in self.closed:
                return dict(w)
        return None

    def activate(self, handle):
        if self.window_info(handle) is None:
            return False
        self.front = handle
        self.states[handle] = "normal" if self.states[handle] == "minimized" else self.states[handle]
        return True

    def get_bounds(self, handle):
        return self.bounds.get(handle) if handle not in self.closed else None

    def set_bounds(self, handle, x, y, width, height):
        self.bounds[handle] = (x, y, width, height)
        return True

    def get_state(self, handle):
        return self.states.get(handle, "normal")

    def set_state(self, handle, state):
        self.states[handle] = state
        return True

    def close_window(self, handle):
        self.closed.append(handle)
        return True

    def monitors(self):
        return [
            {"x": 0, "y": 0, "width": 1920, "height": 1080, "work_x": 0, "work_y": 0, "work_width": 1920, "work_height": 1040, "primary": True, "dpi": 96},
            {"x": 1920, "y": 0, "width": 2560, "height": 1440, "work_x": 1920, "work_y": 0, "work_width": 2560, "work_height": 1400, "primary": False, "dpi": 144},
        ]

    def window_monitor(self, handle):
        return 0

    # User interface
    def msgbox(self, text, title, buttons):
        self.dialogs.append(("msgbox", text, title, buttons))
        return self.answers["msgbox"]

    def ask_text(self, prompt, title, initial):
        self.dialogs.append(("ask_text", prompt, title, initial))
        return self.answers["ask_text"]

    def ask_choice(self, items, prompt, title):
        self.dialogs.append(("ask_choice", items, prompt, title))
        return self.answers["ask_choice"]

    def toast(self, title, text):
        self.toasts.append((title, text))

    # System
    def shell_open(self, target):
        if target == "missing":
            raise OSError("The system cannot find the file specified")
        self.opened.append(target)
