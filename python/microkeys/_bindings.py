# hotkey(), hotstring(), command(), and the other ways scripts bind their
# functions.  Bindings made while a script loads take effect once the whole
# script loaded without an error.  Bindings made while a macro runs take
# effect when it finishes, and last until the scripts next reload.

import inspect
import os
import sys

from ._native import native, OFFLINE
from . import _keyspec

_PACKAGE_DIR = os.path.normcase(os.path.dirname(os.path.abspath(__file__)))


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


def _name(func):
    return getattr(func, "__name__", "a function")


class Entry:
    # One binding.  The runtime gives it an id the first time it's published.
    id = None
    func = None

    def describe(self):
        return ""


class HotkeyEntry(Entry):
    def __init__(self, mods, key, description, when, func, source):
        self.mods = mods
        self.key = key
        self.description = description
        self.when = when
        self.func = func
        self.source = source

    def signature(self):
        return (self.mods, self.key, self.when.fields() if self.when else ("", "", ""))

    def describe(self):
        return f"hotkey {self.description} to {_name(self.func)}"


class HotstringEntry(Entry):
    def __init__(self, trigger, replacement, func, when, immediate, case_sensitive, source):
        self.trigger = trigger
        self.replacement = replacement
        self.func = func
        self.when = when
        self.immediate = immediate
        self.case_sensitive = case_sensitive
        self.source = source

    def signature(self):
        return (self.trigger if self.case_sensitive else self.trigger.lower(), self.when.fields() if self.when else ("", "", ""))

    def describe(self):
        target = repr(self.replacement) if self.replacement is not None else _name(self.func)
        return f"hotstring {self.trigger!r} to {target}"


class CommandEntry(Entry):
    def __init__(self, name, func, source):
        self.name = name
        self.func = func
        self.source = source

    def signature(self):
        return self.name

    def describe(self):
        return f"command {self.name!r} to {_name(self.func)}"


class RemapEntry(Entry):
    # key acting as target, or doing nothing when target is None
    def __init__(self, key, target, when, source):
        self.key = key
        self.target = target
        self.when = when
        self.source = source

    def signature(self):
        return (self.key, self.when.fields() if self.when else ("", "", ""))

    def describe(self):
        return f"remap {self.key} to {self.target or 'nothing'}"


class MessageEntry(Entry):
    # A handler for messages from other machines
    def __init__(self, name, func, source):
        self.name = name
        self.func = func
        self.source = source

    def signature(self):
        return self.name

    def describe(self):
        return f"message {self.name!r} to {_name(self.func)}"


class TimerEntry(Entry):
    # kind is "every" (seconds apart), "after" (once, seconds from created),
    # or "at" (a datetime.time each day, or a datetime.datetime once)
    def __init__(self, kind, seconds, when, func, source, created):
        self.kind = kind
        self.seconds = seconds
        self.when = when
        self.func = func
        self.source = source
        self.created = created
        self.spent = False

    def describe(self):
        if self.kind == "every":
            return f"{_name(self.func)} every {self.seconds:g} seconds"
        if self.kind == "after":
            return f"{_name(self.func)} once, {self.seconds:g} seconds from now"
        return f"{_name(self.func)} at {self.when}"

    def once(self):
        return self.kind == "after" or (self.kind == "at" and hasattr(self.when, "date"))


class WindowEventEntry(Entry):
    def __init__(self, what, event, when, func, source):
        self.what = what
        self.event = event
        self.when = when
        self.func = func
        self.source = source

    def describe(self):
        return f"{self.what}{f' for {self.when}' if self.when else ''} to {_name(self.func)}"


class HandlerEntry(Entry):
    # on_start, on_reload, and clip.on_change
    def __init__(self, what, func, source):
        self.what = what
        self.func = func
        self.source = source

    def describe(self):
        return f"{self.what} to {_name(self.func)}"


class Collector:
    # Everything one script file binds, or (with no file) what macros bind
    def __init__(self, filename):
        self.filename = filename
        self.hotkeys = []
        self.hotstrings = []
        self.commands = []
        self.timers = []
        self.window_events = []
        self.clipboard = []
        self.starts = []
        self.reloads = []
        self.remaps = []
        self.messages = []

    def lists(self):
        return [self.hotkeys, self.hotstrings, self.commands, self.timers, self.window_events,
                self.clipboard, self.starts, self.reloads, self.remaps, self.messages]

    def add(self, items, entry):
        if self.filename is None and hasattr(entry, "signature"):
            # A macro binding the same thing again replaces what it bound before
            items[:] = [x for x in items if x.signature() != entry.signature()]
        items.append(entry)


class _OfflineCollector(Collector):
    # Outside MicroKeys bindings go nowhere, so just say what they'd be
    def add(self, items, entry):
        native.say(f"bind {entry.describe()} (only in MicroKeys)")


_current = None
_offline = _OfflineCollector("")

# Set by the runtime: where bindings made by macros go, and every collector
# whose bindings are in effect
live = None
loaded = []
changed = False


def begin(filename):
    global _current
    _current = Collector(filename)
    return _current


def end():
    global _current
    _current = None


def _collector(what):
    global changed
    if _current is not None:
        return _current
    if live is not None:
        changed = True
        return live
    if OFFLINE:
        return _offline
    raise RuntimeError(f"{what}() can only be used while a script loads or a macro runs")


def _source():
    # "file.py:12" for the line of the user's code that made the binding
    frame = sys._getframe(1)
    while frame is not None:
        fn = frame.f_code.co_filename
        if not os.path.normcase(os.path.abspath(fn)).startswith(_PACKAGE_DIR + os.sep):
            return f"{os.path.basename(fn)}:{frame.f_lineno}"
        frame = frame.f_back
    return "?"


def check_function(func, what, args=0):
    # Catch a function that can't take what it'll be given while the script
    # loads, rather than each time it runs
    if not callable(func):
        raise TypeError(f"{what}() needs a function")
    try:
        inspect.signature(func).bind(*([None] * args))
    except TypeError:
        takes = "no arguments" if args == 0 else "one argument, the window"
        raise TypeError(f"The function for {what}() should take {takes}") from None
    except ValueError:
        pass


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

    A macro can bind hotkeys too, for example to make keys that only work
    until it's done with them.  Those take effect when the macro finishes,
    win over the scripts' own bindings for the same key, and last until
    `unbind()` or the next reload.
    """
    collector = _collector("hotkey")
    mods, key, description = _keyspec.parse_hotkey(combo)
    match = to_match(when)
    source = _source()

    def register(f):
        if not callable(f):
            raise TypeError("hotkey() needs a function")
        collector.add(collector.hotkeys, HotkeyEntry(mods, key, description, match, f, source))
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
        collector.add(collector.hotstrings, HotstringEntry(trigger, None, f, match, immediate, case_sensitive, source))
        return f

    if replacement is None:
        return register
    if isinstance(replacement, str):
        collector.add(collector.hotstrings, HotstringEntry(trigger, replacement, None, match, immediate, case_sensitive, source))
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
        collector.add(collector.commands, CommandEntry(name, f, source))
        return f

    if func is not None:
        return register(func)
    return register


def remap(key, target, when=None):
    # keys.remap(), here with the other bindings
    collector = _collector("remap")
    key = _keyspec.remap_key(key)
    target = None if target is None or target == "" else _keyspec.remap_key(target)
    collector.add(collector.remaps, RemapEntry(key, target, to_match(when), _source()))


def message(name, func):
    # net.on(), here with the other bindings
    collector = _collector("on")
    if not isinstance(name, str) or len(name) == 0:
        raise ValueError("A message handler needs a message name")
    source = _source()

    def register(f):
        if not callable(f):
            raise TypeError("net.on() needs a function")
        collector.add(collector.messages, MessageEntry(name, f, source))
        return f

    if func is not None:
        return register(func)
    return register


def handler(what, attr, func, args=0):
    # The shared part of on_start, on_reload, and clip.on_change: usable as
    # @decorator, @decorator(), or decorator(func)
    collector = _collector(what)
    source = _source()

    def register(f):
        check_function(f, what, args)
        collector.add(getattr(collector, attr), HandlerEntry(what, f, source))
        return f

    if func is not None:
        return register(func)
    return register


def window_event(what, event, when, func):
    # windows.on_activate and windows.on_open, which also work bare
    if func is None and callable(when):
        when, func = None, when
    collector = _collector(what)
    match = to_match(when)
    source = _source()

    def register(f):
        check_function(f, what, 1)
        collector.add(collector.window_events, WindowEventEntry(what, event, match, f, source))
        return f

    if func is not None:
        return register(func)
    return register


def on_start(func=None):
    """Run a function once when MicroKeys starts, after the scripts load.

        @on_start
        def ready():
            ui.toast("MicroKeys is ready")

    It runs like any macro, so the abort hotkey stops it.  A script saved
    after MicroKeys started runs its on_start functions at the next start.
    """
    return handler("on_start", "starts", func)


def on_reload(func=None):
    """Run a function after the scripts reload, because one was saved or
    **Reload scripts** was chosen.  It doesn't run at startup; use
    `on_start` for that.

        @on_reload
        def reloaded():
            ui.toast("Scripts reloaded")
    """
    return handler("on_reload", "reloads", func)


def unbind(func=None, *, hotkey=None, hotstring=None, command=None, remap=None, message=None):
    """Remove bindings.  Returns how many were removed.

        unbind(my_macro)               # everything that runs my_macro
        unbind(hotkey="ctrl+alt+j")    # that key, whatever its when=
        unbind(hotstring=";sig")
        unbind(command="tidy-windows")
        unbind(remap="capslock")       # the key goes back to normal
        unbind(message="lock")         # net.on("lock") handlers

    It covers every kind of binding: hotkeys, hotstrings, commands, timers,
    remaps, events, and message handlers.  Like bindings made by a macro, the
    change lasts until the scripts next reload, when they bind everything
    afresh.
    """
    if func is None and hotkey is None and hotstring is None and command is None and remap is None and message is None:
        raise TypeError("unbind() needs a function, or hotkey=, hotstring=, command=, remap=, or message=")
    if OFFLINE and _current is None and live is None:
        native.say("unbind (only in MicroKeys)")
        return 0
    combo = _keyspec.parse_hotkey(hotkey)[:2] if hotkey is not None else None
    remapped = _keyspec.remap_key(remap) if remap is not None else None

    def doomed(entry):
        if func is not None and entry.func is func:
            return True
        if combo is not None and isinstance(entry, HotkeyEntry):
            return (entry.mods, entry.key) == combo
        if hotstring is not None and isinstance(entry, HotstringEntry):
            return entry.trigger == hotstring or (not entry.case_sensitive and entry.trigger.lower() == hotstring.lower())
        if command is not None and isinstance(entry, CommandEntry):
            return entry.name == command
        if remapped is not None and isinstance(entry, RemapEntry):
            return entry.key == remapped
        if message is not None and isinstance(entry, MessageEntry):
            return entry.name == message
        return False

    global changed
    removed = 0
    collectors = []
    for collector in loaded + [live, _current]:
        if collector is not None and all(collector is not x for x in collectors):
            collectors.append(collector)
    for collector in collectors:
        for items in collector.lists():
            keep = [x for x in items if not doomed(x)]
            removed += len(items) - len(keep)
            items[:] = keep
    if removed and _current is None:
        changed = True
    return removed
