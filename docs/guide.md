# MicroKeys

MicroKeys runs Python functions when you press a hotkey or type a bit of text.  Your macros are ordinary Python 3.14 files with the whole standard library, so a macro can do anything Python can: rewrite the selected text, look something up, arrange windows, or type out a reply.

```python
import time
from microkeys import hotkey, hotstring, keys

@hotkey("ctrl+alt+d")
def type_date():
    keys.type(time.strftime("%Y-%m-%d"))

hotstring(";sig", "Best regards,\nSam")
```

## Installing

Unzip the download into a folder you can write to, such as `%LOCALAPPDATA%\MicroKeys`, and run `MicroKeys.exe`.  MicroKeys updates itself in place, so avoid `Program Files`.

The download isn't code signed, so the first time Windows SmartScreen may say it "protected your PC".  Click **More info**, then **Run anyway**.

MicroKeys lives in the notification area (the tray).  Right-click its icon for the menu, where **Start with Windows** makes it start when you sign in.

## Your scripts

The first time it runs, MicroKeys creates `Documents\MicroKeys` with a starter script, `MicroKeys.py`.  Every `.py` file in that folder is a script.  MicroKeys loads them all when it starts, and again whenever you save one, so changes take effect as soon as you save.

- Files whose names start with `_` aren't loaded as scripts.  Use them for helper code that your scripts import.
- If a script has an error, MicroKeys shows a notification and keeps running the last version of that script that loaded cleanly.  The other scripts aren't affected.
- `print()` and `log()` write to the log window: **Show log** in the tray menu.

## Hotkeys

```python
from microkeys import hotkey, keys

@hotkey("ctrl+shift+u")
def uppercase_selection():
    ...
```

A hotkey is modifiers plus one key, joined with `+`.  The modifiers are `ctrl`, `alt`, `shift`, and `meta` (the Windows key).  `primary` means Ctrl on Windows and Cmd on macOS, for scripts that should work on both.  Keys are single characters (`a`, `7`, `/`) or names such as `f5`, `space`, `enter`, `up`, `numpad1`, or `volumeup`; the API reference lists them all.

`when=` limits a hotkey to some programs.  Elsewhere the key does what it normally does:

```python
from microkeys import hotkey, windows

@hotkey("f5", when="notepad.exe")
def only_in_notepad():
    ...

@hotkey("ctrl+e", when=windows.match(title="* - Excel"))
def only_in_excel():
    ...
```

Patterns are case-insensitive; `*` matches any run of characters and `?` one character.  A key can have one binding per `when=`, plus one without, which applies everywhere else.

When you press a hotkey, MicroKeys waits until you let go of the modifier keys before the macro starts, so they don't mix with the keys it types.

## Hotstrings

A hotstring replaces text as you type it:

```python
from microkeys import hotstring, keys

hotstring(";addr", "221B Baker Street\nLondon")

@hotstring(";now")
def type_time():
    keys.type(time.strftime("%H:%M"))
```

By default a hotstring fires as soon as its last character is typed.  With `immediate=False` it waits for a space, Enter, Tab or punctuation after it, so `btw` doesn't fire in the middle of `btwn`; the ending character is typed again after the replacement.  Triggers that start with a letter or digit only fire at the start of a word.

## Commands

`@command("name")` gives a function a name, so you can run it from a desktop shortcut, the Run dialog, or another program:

```
MicroKeys.exe run tidy-windows
```

If MicroKeys is already running the command runs there; otherwise MicroKeys starts and then runs it.

## Stopping a macro

Press **Ctrl+Alt+Backspace** to stop the macro that's running, or use **Stop running macro** in the tray menu.  While a macro runs for more than a moment the tray icon shows an orange dot.

Macros run one at a time.  If you press another hotkey while one runs, it waits its turn.

## Settings

**Edit settings** in the tray menu opens `settings.json` (in `%APPDATA%\MicroKeys`).  Changes take effect when the scripts next reload.

- `script_folder`: where your scripts live.  Empty means `Documents\MicroKeys`.
- `abort_hotkey`: the key that stops a macro, `"ctrl+alt+backspace"` by default.
- `key_delay_ms`: the pause between keys when typing, 5 by default.  Raise it if a program drops characters.
- `check_updates`: look for a new version once a day.

The log is kept in `%LOCALAPPDATA%\MicroKeys\MicroKeys.log`.

## Editor support

MicroKeys writes a `pyrightconfig.json` into your script folder that points at its copy of the `microkeys` package, so VS Code (with Pylance) and other editors that use Pyright autocomplete the API and check your calls.  Open the script folder itself as the workspace.

## Python

MicroKeys includes Python 3.14 with the full standard library: `json`, `csv`, `re`, `datetime`, `pathlib`, `subprocess`, `urllib`, `sqlite3`, and the rest.  Scripts can import helper modules from the script folder.

## Things to know

- Windows doesn't let a program send keys or clicks to a program running as administrator unless it's an administrator too.  If a macro does nothing in an elevated window, that's why.
- If another program already uses a hotkey, MicroKeys says so in the log and notification, and that hotkey stays with the other program.
- MicroKeys only runs one copy at a time.  Starting it again shows the running copy's log.
