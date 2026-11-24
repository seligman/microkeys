"""Sending keystrokes.

* `press` - press and release key combinations like "ctrl+c"
* `type` - type text, any characters, whatever the keyboard layout
* `down` / `up` - hold keys down and let them go
* `hold` - hold keys down for a `with` block
* `NAMED_KEYS` - every key name besides single characters
"""

from contextlib import contextmanager

from ._native import native
from . import _keyspec, _settings, windows

NAMED_KEYS = list(_keyspec.NAMED_KEYS)


def _delay(delay):
    if delay is None:
        return int(_settings.current.get("key_delay_ms", 5))
    return round(delay * 1000)


def _hold(hold=None):
    if hold is None:
        return int(_settings.current.get("key_hold_ms", 15))
    return round(hold * 1000)


def _frame():
    # directinput_key_ms if the program in front uses DirectInput, going by
    # the directinput_programs setting and then by what it has loaded, else 0
    ms = int(_settings.current.get("directinput_key_ms", 40))
    if ms <= 0:
        return 0
    programs = dict(_settings.current.get("directinput_programs") or {})
    if programs:
        info = native.foreground()
        process = info["process"] if info else ""
        for pattern, uses in programs.items():
            if windows._pattern(pattern).fullmatch(process):
                return ms if uses else 0
    return ms if native.uses_directinput() else 0


def press(*combos, delay=None):
    """Press and release one or more key combinations, in order.

        keys.press("ctrl+c")
        keys.press("ctrl+a", "delete")
        keys.press("primary+v")       # Ctrl+V on Windows, Cmd+V on macOS

    A combination is any number of keys joined with `+`; they go down in
    order and come up in reverse.  `delay` is the pause between key events in
    seconds, defaulting to the key_delay_ms setting; each key stays down at
    least as long as the key_hold_ms setting.
    """
    actions = []
    for combo in combos:
        names = _keyspec.split_combo(combo)
        actions.extend((0, name) for name in names)
        actions.extend((1, name) for name in reversed(names))
    native.send_keys(actions, _delay(delay), _hold(), _frame())


def down(*names, delay=None):
    """Push keys down and leave them down.  Pair with `up()`, or use `hold()`."""
    native.send_keys([(0, _keyspec.canonical(n)) for n in names], _delay(delay), _hold(), _frame())


def up(*names, delay=None):
    """Let keys go that `down()` pushed."""
    native.send_keys([(1, _keyspec.canonical(n)) for n in names], _delay(delay), _hold(), _frame())


@contextmanager
def hold(*names):
    """Hold keys down while a `with` block runs.

        with keys.hold("shift"):
            keys.press("down", "down", "down")
    """
    down(*names)
    try:
        yield
    finally:
        up(*reversed(names))


def type(text, delay=None, hold=None):
    """Type text exactly as given.

    Any character works, whatever the keyboard layout: accents, symbols, and
    emoji are sent as text.  A newline presses Enter and a tab presses Tab.
    For a lot of text, `clip.paste_text()` is faster.

    Each key stays down for `hold` seconds, and `delay` seconds pass before
    the next one, defaulting to the key_hold_ms and key_delay_ms settings.
    Raise them if a program drops characters.  In a program that uses
    DirectInput, mostly games, both last at least the directinput_key_ms
    setting, and characters go out as the keys that type them.
    """
    if not isinstance(text, str):
        text = str(text)
    native.send_keys([(2, ch) for ch in text if ch != "\r"], _delay(delay), _hold(hold), _frame())
