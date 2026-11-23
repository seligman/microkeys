"""MicroKeys: hotkeys, hotstrings, and desktop automation in Python.

    from microkeys import hotkey, hotstring, keys

    @hotkey("ctrl+alt+d")
    def type_date():
        keys.type(time.strftime("%Y-%m-%d"))

    hotstring(";sig", "Best regards,\\nSam")

Binding:
* `hotkey` - run a function when a key combination is pressed
* `hotstring` - replace or react to text as it's typed
* `command` - name a function so `MicroKeys.exe run <name>` can start it
* `every`, `after`, `at` - run a function on a schedule
* `on_start`, `on_reload` - run a function when MicroKeys starts or the scripts reload
* `unbind` - remove bindings

Modules:
* `keys` - send keystrokes and type text
* `clip` - the clipboard
* `mouse` - the mouse
* `windows` - find, arrange, and switch windows, and react to them
* `ui` - message boxes, questions, and notifications

Also `store`, a dict kept between runs, and `log`, `sleep`, `run`, `open`,
and `paste_text`.

Outside MicroKeys (in plain Python, or an editor) the package still
imports, so editors can check scripts.  Nothing reaches the desktop then:
each call says what it would have done.
"""

from ._bindings import hotkey, hotstring, command, on_start, on_reload, unbind
from ._timers import every, after, at
from ._system import log, sleep, run, open, version
from ._store import store
from . import keys, clip, mouse, windows, ui
from .clip import paste_text

# open() is left out so "from microkeys import *" doesn't hide the built-in
__all__ = ["hotkey", "hotstring", "command", "on_start", "on_reload", "unbind", "every", "after", "at",
           "keys", "clip", "mouse", "windows", "ui", "store", "log", "sleep", "run", "paste_text", "version"]
