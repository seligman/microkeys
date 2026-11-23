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

Modules:
* `keys` - send keystrokes and type text
* `clip` - the clipboard
* `mouse` - the mouse
* `windows` - find, arrange, and switch windows
* `ui` - message boxes, questions, and notifications

Also `log`, `sleep`, `run`, `open`, and `paste_text`.
"""

from ._bindings import hotkey, hotstring, command
from ._system import log, sleep, run, open, version
from . import keys, clip, mouse, windows, ui
from .clip import paste_text

# open() is left out so "from microkeys import *" doesn't hide the built-in
__all__ = ["hotkey", "hotstring", "command", "keys", "clip", "mouse", "windows", "ui",
           "log", "sleep", "run", "paste_text", "version"]
