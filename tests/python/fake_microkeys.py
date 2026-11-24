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
        self.overlays = []
        self.sent = []
        self.timings = []
        self.directinput = set()
        self.clipboard = None
        self.clip_other = {}
        self.clip_seq = 1
        self.concealed = False
        self.posts = []
        self.is_paused = False
        self.queue_full = False
        self.console_more = []
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
        self.elevated = False
        self.elevated_windows = set()
        # The simulated screen: (width, height, BGRA pixels), all one grey
        self.screen = (64, 48, b"\x40\x40\x40\xff" * (64 * 48))
        self.captures = []
        # Controls: handle to info dict, plus "parent" and "text" keys
        self.controls = {}
        # Text: words "painted" on the simulated screen for OCR, as (text, x,
        # y, width, height, line), and what accessibility finds, for
        # documents and for names
        self.screen_words = []
        self.ocr_error = None
        self.ocr_reads = []
        self.ocr_languages = ["en-US"]
        self.text_hits = {True: [], False: []}
        self.text_searches = []
        self.focused_control = 0
        self.control_actions = []
        self.released = []
        self.file_answer = None
        self.file_dialogs = []
        self.advertised = []
        self.bonjour = {}

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

    def configure(self, script_dir, abort_mods, abort_key, feed, check, editor, run_elevated):
        self.config = (script_dir, abort_mods, abort_key, feed, check, editor, run_elevated)

    def set_bindings(self, hotkeys, hotstrings, commands, window_events, remaps):
        self.bindings = (hotkeys, hotstrings, commands, window_events, remaps)

    def scripts_loaded(self, ok, summary):
        self.loaded.append((ok, summary))

    def macro_running(self, what):
        self.running = what

    def console_state(self, more):
        self.console_more.append(more)

    def post(self, kind, id, extra):
        if self.queue_full:
            return False
        self.posts.append((kind, id, extra))
        return True

    def paused(self):
        return self.is_paused

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

    def uses_directinput(self):
        return self.foreground()["process"] in self.directinput

    def send_keys(self, actions, delay, hold, frame):
        self.timings.append((delay, hold, frame))
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
        self.clip_other = {}
        self.clip_seq += 1
        return True

    def clip_sequence(self):
        return self.clip_seq

    def clip_kinds(self):
        return (["text"] if self.clipboard is not None else []) + list(self.clip_other)

    def clip_get_data(self, kind):
        if kind == "text":
            return None if self.clipboard is None else self.clipboard.encode("utf-8")
        return self.clip_other.get(kind)

    def clip_set_data(self, items):
        items = dict(items)
        text = items.pop("text", None)
        self.clipboard = None if text is None else text.decode("utf-8")
        self.clip_other = items
        self.clip_seq += 1
        return True

    def clip_save(self):
        return [(kind, self.clip_get_data(kind)) for kind in self.clip_kinds()]

    def clip_restore(self, items):
        return self.clip_set_data(items)

    def clip_concealed(self):
        return self.concealed

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

    def window_elevated(self, handle):
        return handle in self.elevated_windows

    # Screen
    def paint(self, x, y, width, height, rgb):
        # Fill part of the simulated screen with a colour
        w, h, pixels = self.screen
        pixels = bytearray(pixels)
        for row in range(y, y + height):
            for col in range(x, x + width):
                i = (row * w + col) * 4
                pixels[i:i + 4] = bytes((rgb[2], rgb[1], rgb[0], 255))
        self.screen = (w, h, bytes(pixels))

    def screen_capture(self, x, y, width, height):
        # Off the edge of the simulated screen is black
        self.captures.append((x, y, width, height))
        w, h, pixels = self.screen
        black = b"\x00\x00\x00\xff"
        rows = []
        for row in range(y, y + height):
            if not 0 <= row < h:
                rows.append(black * width)
                continue
            left, right = max(x, 0), min(x + width, w)
            line = pixels[(row * w + left) * 4:(row * w + right) * 4] if left < right else b""
            rows.append(black * (min(left, x + width) - x) + line + black * (x + width - max(right, x)))
        return b"".join(rows)

    def image_decode(self, data):
        from microkeys import _offline
        return _offline.image_decode(data)

    def image_encode(self, width, height, pixels):
        from microkeys import _offline
        return _offline.image_encode(width, height, pixels)

    def image_find(self, width, height, pixels, needle_width, needle_height, needle, tolerance, limit):
        from microkeys import _offline
        return _offline.image_find(width, height, pixels, needle_width, needle_height, needle, tolerance, limit)

    def text_read(self, width, height, pixels, language, scale):
        # The words inside the last area captured, as OCR would see them
        if self.ocr_error:
            raise OSError(self.ocr_error)
        x, y, w, h = self.captures[-1]
        self.ocr_reads.append(((x, y, w, h), language, scale))
        return [(text, wx - x, wy - y, ww, wh, line) for text, wx, wy, ww, wh, line in self.screen_words
                if wx >= x and wy >= y and wx + ww <= x + w and wy + wh <= y + h]

    def text_languages(self):
        return list(self.ocr_languages)

    # Controls
    def add_control(self, handle, parent=0, **info):
        base = {"name": "", "role": "pane", "automation_id": "", "class_name": "", "runtime_id": str(handle), "enabled": True,
                "focused": False, "offscreen": False, "bounds": (0, 0, 10, 10), "window": 0x101, "has_value": False,
                "read_only": True, "value": "", "toggle": -1, "selected": -1, "expanded": -1, "actions": [], "parent": parent, "text": None}
        base.update(info)
        self.controls[handle] = base

    def _matches(self, info, name, role, automation_id, class_name):
        from microkeys import windows
        return ((not name or windows._pattern(name).fullmatch(info["name"])) and (not role or role == info["role"]) and
                (not automation_id or automation_id == info["automation_id"]) and
                (not class_name or windows._pattern(class_name).fullmatch(info["class_name"])))

    def _descendants(self, handle):
        for h, info in self.controls.items():
            if info["parent"] == handle:
                yield h
                yield from self._descendants(h)

    def control_find(self, root, window, name, role, automation_id, class_name, all):
        if root == 0:
            window = window or self.front
            starts = [h for h, info in self.controls.items() if info["parent"] == 0 and info["window"] == window]
            pool = starts + [d for h in starts for d in self._descendants(h)]
        else:
            pool = list(self._descendants(root))
        found = [h for h in pool if self._matches(self.controls[h], name, role, automation_id, class_name)]
        return found if all else found[:1]

    def control_info(self, handle):
        info = self.controls.get(handle)
        return None if info is None else {k: v for k, v in info.items() if k not in ("parent", "text")}

    def control_text(self, handle):
        info = self.controls.get(handle)
        if info is None:
            return None
        return info["text"] if info["text"] is not None else info["value"] if info["has_value"] else info["name"]

    def control_do(self, handle, action, value):
        info = self.controls.get(handle)
        if info is None:
            raise ValueError("That control is gone")
        wanted = {"focus": None, "set_value": "set_value", "click": "click", "toggle": "toggle", "select": "select",
                  "expand": "expand", "collapse": "collapse", "scroll": "scroll"}[action]
        if wanted is not None and wanted not in info["actions"]:
            raise ValueError(f"The control can't {action}")
        self.control_actions.append((handle, action, value))
        if action == "set_value":
            info["value"] = value
        elif action == "toggle":
            info["toggle"] = 0 if info["toggle"] == 1 else 1

    def control_related(self, handle, parent):
        info = self.controls.get(handle)
        if info is None:
            return []
        if parent:
            return [info["parent"]] if info["parent"] else []
        return [h for h, other in self.controls.items() if other["parent"] == handle]

    def control_focused(self):
        return self.focused_control

    def control_at(self, x, y):
        for h, info in self.controls.items():
            bx, by, bw, bh = info["bounds"]
            if bx <= x < bx + bw and by <= y < by + bh and not any(self.controls[c]["parent"] == h for c in self.controls):
                return h
        return 0

    def control_release(self, handle):
        self.released.append(handle)

    def control_text_find(self, root, window, text, match_case, documents, limit):
        self.text_searches.append((window, text, match_case, documents, limit))
        if isinstance(self.text_hits, Exception):
            raise self.text_hits
        return self.text_hits[documents][:limit]

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

    def overlay(self, text, timeout_ms):
        self.overlays.append((text, timeout_ms))

    def ask_file(self, save, title, types, initial):
        self.file_dialogs.append((save, title, types, initial))
        return self.file_answer

    # Network
    def net_advertise(self, name, port):
        self.advertised.append((name, port))

    def net_resolve(self, name, timeout_ms):
        return self.bonjour.get(name)

    def net_browse(self, timeout_ms):
        return list(self.bonjour)

    # System
    def shell_open(self, target):
        if target == "missing":
            raise OSError("The system cannot find the file specified")
        self.opened.append(target)

    def is_elevated(self):
        return self.elevated
