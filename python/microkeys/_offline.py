# A stand-in for the _microkeys module, used when the microkeys package runs
# outside MicroKeys: in plain Python, a test of your own, or an editor.
# Nothing reaches the desktop.  Each call that would have done something says
# so on stderr and returns something harmless, so a script can be imported
# and its functions called to see what they'd do.

import sys
import time

OFFLINE = True

_clipboard = {}
_sequence = 1
_pointer = [0, 0]


def say(text):
    print(f"[microkeys] {text}", file=sys.stderr)


def _short(text, limit=60):
    return repr(text if len(text) <= limit else text[:limit] + "...")


def _describe_keys(actions):
    # [(0, "ctrl"), (0, "c"), (1, "c"), (1, "ctrl")] -> "press ctrl+c"
    out, text, down, pressed = [], "", [], False
    for kind, value in actions:
        if kind == 2:
            text += value
            continue
        if text:
            out.append(f"type {_short(text)}")
            text = ""
        if kind == 0:
            down.append(value)
            pressed = False
        elif value in down:
            if not pressed and down[-1] == value:
                out.append("press " + "+".join(down))
                pressed = True
            down.remove(value)
        else:
            out.append(f"let go of {value}")
    if text:
        out.append(f"type {_short(text)}")
    if down:
        out.append("hold down " + "+".join(down))
    return ", ".join(out)


# General

def version():
    try:
        from importlib import metadata
        return metadata.version("microkeys")
    except Exception:
        return "0.0.0"


def platform():
    if sys.platform == "win32":
        return "windows"
    return "macos" if sys.platform == "darwin" else "linux"


def log(level, text):
    if level == 3:
        return
    print(text, file=sys.stderr if level == 2 else sys.stdout)


def configure(script_dir, abort_mods, abort_key, update_feed, check_updates, editor):
    pass


def set_bindings(hotkeys, hotstrings, commands, window_events):
    pass


def scripts_loaded(ok, summary):
    pass


def macro_running(what):
    pass


def console_state(more):
    pass


def post(kind, id, extra):
    return False


def paused():
    return False


def cancelled():
    return False


def check_cancel():
    pass


def sleep(seconds):
    time.sleep(max(0.0, seconds))


def wait_modifiers(timeout):
    return True


# Keyboard

def is_key(name):
    return True


def send_keys(actions, delay_ms):
    if actions:
        say(_describe_keys(actions))


# Mouse

def cursor():
    return tuple(_pointer)


def move_cursor(x, y):
    _pointer[:] = [x, y]
    say(f"move the mouse to ({x}, {y})")


def mouse_button(button, down):
    if button not in ("left", "right", "middle", "x1", "x2"):
        raise ValueError(f"Unknown mouse button: {button}")
    say(f"{button} button {'down' if down else 'up'}")


def mouse_wheel(clicks, horizontal):
    say(f"scroll {'sideways ' if horizontal else ''}{clicks}")


# Clipboard, kept in memory

def _changed(items):
    global _sequence
    _clipboard.clear()
    _clipboard.update(items)
    _sequence += 1


def clip_get():
    data = _clipboard.get("text")
    return None if data is None else data.decode("utf-8")


def clip_set(text):
    _changed({"text": text.encode("utf-8")})
    say(f"put {_short(text)} on the clipboard")
    return True


def clip_sequence():
    return _sequence


def clip_kinds():
    return list(_clipboard)


def clip_get_data(kind):
    return _clipboard.get(kind)


def clip_set_data(items):
    _changed(dict(items))
    say("put " + ", ".join(kind for kind, data in items) + " on the clipboard")
    return True


def clip_save():
    return list(_clipboard.items())


def clip_restore(items):
    _changed(dict(items))
    return True


def clip_concealed():
    return False


# Windows: there are none

def list_windows(include_hidden):
    return []


def foreground():
    return None


def window_info(handle):
    return None


def activate(handle):
    return False


def get_bounds(handle):
    return None


def set_bounds(handle, x, y, width, height):
    return False


def get_state(handle):
    return "normal"


def set_state(handle, state):
    return False


def close_window(handle):
    return False


def monitors():
    return [{"x": 0, "y": 0, "width": 1920, "height": 1080, "work_x": 0, "work_y": 0, "work_width": 1920,
             "work_height": 1040, "primary": True, "dpi": 96}]


def window_monitor(handle):
    return 0


# User interface: every question gets its default answer

def msgbox(text, title, buttons):
    say(f"message box {title!r}: {_short(text)}")
    return "yes" if buttons.startswith("yes") else "ok"


def ask_text(prompt, title, initial):
    say(f"ask {_short(prompt)}, answering {initial!r}")
    return initial


def ask_choice(items, prompt, title):
    say(f"ask to choose from {len(items)} items, answering {items[0]!r}")
    return 0


def toast(title, text):
    say(f"notification {title!r}: {_short(text)}")


def overlay(text, timeout_ms):
    say(f"show {_short(text)} on screen" if text else "take the message off the screen")


def shell_open(target):
    say(f"open {target}")
