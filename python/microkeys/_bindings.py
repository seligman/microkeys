# hotkey(), hotstring() and command(): the decorators scripts use to bind
# their functions.  Bindings are collected while a script loads, and only
# take effect once the whole script loaded without an error.

import os
import sys

from . import _keyspec


class Match:
    """Which windows a hotkey or hotstring applies to.

    Each field is a case-insensitive pattern where `*` matches any run of
    characters and `?` matches one.  Fields left as None match anything.
    Build one with `windows.app()` or `windows.match()`.
    """

    def __init__(self, process=None, title=None, class_name=None):
        self.process = process
        self.title = title
        self.class_name = class_name

    def fields(self):
        return (self.process or "", self.title or "", self.class_name or "")

    def __repr__(self):
        parts = [f"{k}={v!r}" for k, v in (("process", self.process), ("title", self.title), ("class_name", self.class_name)) if v]
        return f"Match({', '.join(parts)})"


def to_match(when):
    # when= accepts None, a process name, or a Match
    if when is None:
        return None
    if isinstance(when, Match):
        return when
    if isinstance(when, str):
        return Match(process=when)
    raise TypeError("when= takes a process name like 'notepad.exe' or windows.match(...)")


class HotkeyEntry:
    def __init__(self, mods, key, description, when, func, source):
        self.mods = mods
        self.key = key
        self.description = description
        self.when = when
        self.func = func
        self.source = source


class HotstringEntry:
    def __init__(self, trigger, replacement, func, when, immediate, case_sensitive, source):
        self.trigger = trigger
        self.replacement = replacement
        self.func = func
        self.when = when
        self.immediate = immediate
        self.case_sensitive = case_sensitive
        self.source = source


class CommandEntry:
    def __init__(self, name, func, source):
        self.name = name
        self.func = func
        self.source = source


class Collector:
    # Everything one script file binds
    def __init__(self, filename):
        self.filename = filename
        self.hotkeys = []
        self.hotstrings = []
        self.commands = []


_current = None


def begin(filename):
    global _current
    _current = Collector(filename)
    return _current


def end():
    global _current
    _current = None


def _collector(what):
    if _current is None:
        raise RuntimeError(f"{what}() can only be used while a script loads, at the top level of the script")
    return _current


def _source():
    # "file.py:12" for the line in the user's script that made the binding
    frame = sys._getframe(2)
    while frame is not None:
        if _current is not None and frame.f_code.co_filename == _current.filename:
            return f"{os.path.basename(frame.f_code.co_filename)}:{frame.f_lineno}"
        frame = frame.f_back
    return "?"


def hotkey(combo, func=None, *, when=None):
    """Run a function when a key combination is pressed.

    Use it as a decorator:

        @hotkey("ctrl+alt+z")
        def my_macro():
            keys.type("Hello")

    `combo` is modifiers plus one key, joined with `+`: `ctrl`, `alt`,
    `shift`, `meta` (the Windows key, Cmd on macOS) or `primary` (Ctrl on
    Windows, Cmd on macOS), then a key such as `z`, `f5`, `space` or
    `numpad1`.  See `keys.NAMED_KEYS` for every name.

    `when` limits the hotkey to some windows: a process name such as
    `"notepad.exe"`, or `windows.match(title="*Excel*")`.  Elsewhere the key
    goes to the program as usual.  The same key can have one binding per
    `when`, plus one without.
    """
    collector = _collector("hotkey")
    mods, key, description = _keyspec.parse_hotkey(combo)
    match = to_match(when)
    source = _source()

    def register(f):
        if not callable(f):
            raise TypeError("hotkey() needs a function")
        collector.hotkeys.append(HotkeyEntry(mods, key, description, match, f, source))
        return f

    if func is not None:
        return register(func)
    return register


def hotstring(trigger, replacement=None, *, when=None, immediate=True, case_sensitive=False):
    """Replace or react to text as the user types it.

    With a replacement string, typing the trigger replaces it:

        hotstring(";sig", "Best regards,\\nSam")

    As a decorator, the trigger is erased and the function runs:

        @hotstring(";date")
        def type_date():
            keys.type(time.strftime("%Y-%m-%d"))

    By default the hotstring fires the moment its last character is typed.
    With `immediate=False` it waits for a space, Enter, Tab or punctuation
    after it (so "btw" doesn't fire inside "btwn"), and that character is
    typed again after the replacement.  A trigger that starts with a letter
    or digit only fires at the start of a word.  `when` works as for hotkey().
    """
    collector = _collector("hotstring")
    if not isinstance(trigger, str) or len(trigger) == 0:
        raise ValueError("A hotstring needs a non-empty trigger string")
    match = to_match(when)
    source = _source()

    def register(f):
        if not callable(f):
            raise TypeError("hotstring() needs a function or a replacement string")
        collector.hotstrings.append(HotstringEntry(trigger, None, f, match, immediate, case_sensitive, source))
        return f

    if replacement is None:
        return register
    if isinstance(replacement, str):
        collector.hotstrings.append(HotstringEntry(trigger, replacement, None, match, immediate, case_sensitive, source))
        return None
    return register(replacement)


def command(name, func=None):
    """Give a function a name so it can be run from outside MicroKeys.

        @command("tidy-windows")
        def tidy():
            ...

    Run it with `MicroKeys.exe run tidy-windows`, for example from a desktop
    shortcut or another program.
    """
    collector = _collector("command")
    if not isinstance(name, str) or len(name) == 0:
        raise ValueError("A command needs a name")
    source = _source()

    def register(f):
        if not callable(f):
            raise TypeError("command() needs a function")
        collector.commands.append(CommandEntry(name, f, source))
        return f

    if func is not None:
        return register(func)
    return register
