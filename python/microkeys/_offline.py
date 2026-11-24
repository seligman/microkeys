# A stand-in for the _microkeys module, used when the microkeys package runs
# outside MicroKeys: in plain Python, a test of your own, or an editor.
# Nothing reaches the desktop.  Each call that would have done something says
# so on stderr and returns something harmless, so a script can be imported
# and its functions called to see what they'd do.

import struct
import sys
import time
import zlib

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


def configure(script_dir, abort_mods, abort_key, update_feed, check_updates, editor, run_elevated):
    pass


def set_bindings(hotkeys, hotstrings, commands, window_events, remaps):
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


def uses_directinput():
    return False


def send_keys(actions, delay_ms, hold_ms, frame_ms):
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


def window_elevated(handle):
    return False


# Screen and images.  The screen is blank; images are read and written as
# PNG in Python, so finding one image in another works.

def screen_capture(x, y, width, height):
    say(f"capture the screen at ({x}, {y}), {width}x{height}")
    return b"\x00\x00\x00\xff" * (width * height)


def _chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def image_encode(width, height, pixels):
    rows = []
    for y in range(height):
        row = pixels[y * width * 4:(y + 1) * width * 4]
        rgba = bytearray(row)
        rgba[0::4], rgba[2::4] = row[2::4], row[0::4]
        rows.append(b"\x00" + bytes(rgba))
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + _chunk(b"IHDR", header) + _chunk(b"IDAT", zlib.compress(b"".join(rows))) + _chunk(b"IEND", b"")


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def image_decode(data):
    # 8-bit PNGs that aren't interlaced, which is what screenshots and most
    # editors write
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Outside MicroKeys only PNG images can be read")
    pos, idat, palette, alpha = 8, b"", None, None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = body
        elif kind == b"tRNS":
            alpha = body
        elif kind == b"IDAT":
            idat += body
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if depth != 8 or interlace != 0 or channels is None:
        raise ValueError("Outside MicroKeys only 8-bit PNG images that aren't interlaced can be read")
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray()
    prev = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            line[i] = (line[i] + (0, a, b, (a + b) // 2, _paeth(a, b, c))[kind]) & 0xFF
        for x in range(width):
            px = line[x * channels:(x + 1) * channels]
            if color == 0:
                r = g = b = px[0]
                a = 255
            elif color == 2:
                r, g, b = px
                a = 255
            elif color == 3:
                r, g, b = palette[px[0] * 3:px[0] * 3 + 3]
                a = alpha[px[0]] if alpha is not None and px[0] < len(alpha) else 255
            elif color == 4:
                r = g = b = px[0]
                a = px[1]
            else:
                r, g, b, a = px
            out += bytes((b, g, r, a))
        prev = line
    return (width, height, bytes(out))


def image_find(width, height, pixels, needle_width, needle_height, needle, tolerance, limit):
    # The same search the app does natively, slowly
    points = [(x, y) for y in range(needle_height) for x in range(needle_width) if needle[(y * needle_width + x) * 4 + 3] >= 128]
    found = []
    for y in range(height - needle_height + 1):
        for x in range(width - needle_width + 1):
            hit = True
            for px, py in points:
                i = ((y + py) * width + x + px) * 4
                j = (py * needle_width + px) * 4
                if any(abs(pixels[i + k] - needle[j + k]) > tolerance for k in range(3)):
                    hit = False
                    break
            if hit and not any(abs(fx - x) < needle_width and abs(fy - y) < needle_height for fx, fy in found):
                found.append((x, y))
                if len(found) >= limit:
                    return found
    return found


# Text: a blank screen has none to read, and there's no OCR here

def text_read(width, height, pixels, language, scale):
    return []


def text_languages():
    return []


# Controls: there are none

def control_find(root, window, name, role, automation_id, class_name, all):
    return []


def control_info(handle):
    return None


def control_text(handle):
    return None


def control_do(handle, action, value):
    raise ValueError("That control is gone")


def control_related(handle, parent):
    return []


def control_focused():
    return 0


def control_at(x, y):
    return 0


def control_release(handle):
    pass


def control_text_find(root, window, text, match_case, documents, limit):
    return []


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


def ask_file(save, title, types, initial):
    say(f"ask for a file to {'save to' if save else 'open'}, answering nothing")
    return None


# Network: nothing listens, and no other machines are found

def net_advertise(name, port):
    pass


def net_resolve(name, timeout_ms):
    return None


def net_browse(timeout_ms):
    return []


def shell_open(target):
    say(f"open {target}")


def is_elevated():
    return False
