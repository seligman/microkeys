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

Unzip the download into a folder you can write to, such as `%LOCALAPPDATA%\MicroKeys`, and run `MicroKeys.exe`.
MicroKeys updates itself in place, so avoid `Program Files`. <!-- updates -->

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

### Mouse buttons

A hotkey can be a mouse button or a turn of the wheel, with or without modifiers: `mouseleft`, `mouseright`, `mousemiddle`, `mousex1` and `mousex2` (the side buttons), and `wheelup`, `wheeldown`, `wheelleft` and `wheelright`.

```python
@hotkey("mousex1", when="explorer.exe")
def up_a_folder():
    keys.press("alt+up")

@hotkey("ctrl+shift+wheelup")
def louder():
    keys.press("volumeup")
```

The program under the pointer doesn't see a click or a wheel turn that's a hotkey.  `mouseleft` needs a modifier, since without one it would take every click.

## Remapping keys

`keys.remap()` makes one key act as another for as long as MicroKeys runs, or (with `None`) do nothing at all:

```python
from microkeys import keys

keys.remap("capslock", "ctrl")
keys.remap("insert", None)
keys.remap("rctrl", "apps", when="code.exe")
```

A remapped key is held as long as you hold it, so it works in combinations: with Caps Lock as Ctrl, Caps Lock+C copies.  `when=` works as it does for hotkeys.  Keys that macros send aren't remapped, and `unbind(remap="capslock")` puts a key back.

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

## Timers

```python
import datetime
from microkeys import every, after, at, ui

@every(minutes=30)
def stretch():
    ui.toast("Time to stretch")

@at("17:30")
def go_home():
    ui.toast("Go home")

after(10, lambda: ui.toast("Ten seconds since the scripts loaded"))
at(datetime.datetime(2026, 12, 24, 15, 0), lambda: ui.msgbox("Holidays!"))
```

`every()` runs a function at a fixed interval, `after()` runs it once after a delay, and `at()` runs it at a time of day (every day), or once at a date and time.  Timers take their turn with hotkeys, so a timer never interrupts a running macro; it waits for it to finish.  A daily time that passes while the computer sleeps or MicroKeys is paused is skipped until the next day, and an `every()` run is skipped; a one-time timer waits and runs when MicroKeys is resumed.

## Window events

```python
from microkeys import windows, keys

@windows.on_activate(when="excel.exe")
def excel_in_front(win):
    ...

@windows.on_open(when=windows.match(title="Save As"))
def save_dialog(win):
    keys.type("report.txt")
```

`windows.on_activate` runs when a window comes to the front, and `windows.on_open` when a new window appears.  The function is given the `Window`.  `when=` works as it does for hotkeys; without it the function runs for every window.

## Starting up and reloading

`@on_start` runs a function once, when MicroKeys starts and the scripts have loaded.  `@on_reload` runs one each time the scripts reload, but not at startup.

```python
from microkeys import on_start, run, windows

@on_start
def open_my_apps():
    if not windows.find(process="outlook.exe"):
        run("outlook.exe")
```

## Changing bindings from a macro

A macro can call `hotkey()`, `hotstring()`, `every()` and the rest too.  What it binds takes effect when the macro finishes, wins over the scripts' own bindings for the same key (a hotkey without `when=` takes the key in every window), and lasts until the scripts next reload.  `unbind()` takes bindings away again, so a hotkey can switch on a set of keys and another can switch them off:

```python
from microkeys import hotkey, unbind, keys

@hotkey("ctrl+alt+j")
def arrow_keys_on():
    hotkey("j", lambda: keys.press("down"))
    hotkey("k", lambda: keys.press("up"))
    hotkey("escape", arrow_keys_off)

def arrow_keys_off():
    for key in ("j", "k", "escape"):
        unbind(hotkey=key)
```

`unbind(func)` removes everything that runs `func`; `unbind(hotkey=...)`, `unbind(hotstring=...)`, `unbind(command=...)`, `unbind(remap=...)` and `unbind(message=...)` remove one binding by what triggers it.

## The clipboard

`clip.get()` and `clip.set()` handle text.  `clip.set()` also takes `html=`, `rtf=`, `files=` and `image=` (PNG data), together or on their own, and `clip.get_html()`, `clip.get_rtf()`, `clip.get_files()` and `clip.get_image()` read them back.  `clip.formats()` says what's there.

```python
from microkeys import hotkey, clip

@hotkey("ctrl+alt+l")
def paste_link():
    url = clip.get()
    clip.set(url, html=f'<a href="{url}">{url}</a>')
```

`clip.save()` keeps a copy of everything on the clipboard and `clip.restore()` puts it back; `paste_text()` uses them, so pasting text through the clipboard doesn't lose an image that was there.

`@clip.on_change` runs a function when another program changes the clipboard, and `clip.history()` lists the text copied recently, newest first.  The history is kept in memory only, and leaves out what password managers mark as private.

## The screen

`screen` reads colours from the screen, takes screenshots, and finds pictures on it, for programs that offer no better way in:

```python
from microkeys import hotkey, screen

@hotkey("ctrl+alt+o")
def click_ok():
    found = screen.wait_for("ok_button.png", timeout=5)
    if found:
        found.click()
```

`screen.capture()` takes a screenshot, of every monitor or of a window, a monitor, or an area, and `.save()` writes it as a PNG; save a small piece of one, or crop a screenshot in an image editor, to have something to look for.  `screen.find()` looks for an image once, `screen.wait_for()` keeps looking until it appears, and `screen.pixel()` and `screen.wait_for_color()` work with single points.  `tolerance=` lets colours differ a little, and fully transparent pixels in a PNG match anything.  Coordinates are screen pixels, as for the mouse.

### Text on screen

`screen.find_text()` finds where some text is showing, in any program, web pages included, and `.click()` clicks the middle of it:

```python
@hotkey("ctrl+alt+c")
def continue_reading():
    screen.find_text("Click here to continue").click()
```

It looks two ways.  First it asks the window in front (or the window given as `region=`) for its text through accessibility, the same text screen readers use; that's exact and quick, and finds text anywhere in a web page in Chrome, Edge, and Firefox.  If that finds nothing, it reads the screen with OCR, which works on anything showing text, pictures of text and games included, but can misread text that's very small, faint, or fancy.  `method="controls"` or `method="ocr"` uses just one way.  Both run on your own computer; the OCR is the one built into Windows.

Letters and digits are compared, not spaces or punctuation, case doesn't matter (unless `match_case=True`), and only whole words match, so "here" isn't found in "where".  Text that's scrolled away or covered by another window isn't found.  `screen.find_all_text()` finds every place, `screen.wait_for_text()` waits for text to appear, and `screen.read_text()` reads all the text in a window or area, line by line.  OCR reads the languages Windows has OCR for, normally the one it was set up in; `screen.text_languages()` lists them, and `language="fr-FR"` picks one.

## Controls in windows

`controls` finds the buttons, text boxes, check boxes and other controls inside windows and works them directly, through UI Automation.  Unlike clicking at a position, that keeps working when a window moves or its layout changes:

```python
from microkeys import hotkey, controls, windows

@hotkey("ctrl+alt+s")
def search_invoices():
    win = windows.find_one(process="myapp.exe")
    controls.find(automation_id="SearchBox", window=win).value = "invoices"
    controls.find(name="Search", role="button", window=win).click()
```

Look controls up by `name` (what the control says, or its label), `role` (such as `button`, `edit`, `checkbox`, `menuitem`, or `listitem`), `automation_id` (the program's own name for it, which doesn't change with the language) or `class_name`, in the window in front or the one given.  To see what a control is called, point at it and enter `controls.at(*mouse.position())` at the log window's prompt.

## Messages between machines

A macro on one computer can run a handler on another, say a hotkey on your laptop that locks your desktop, or a build machine telling you it's done:

```python
from microkeys import hotkey, net, run, ui

# On the laptop
@hotkey("ctrl+alt+l")
def lock_desktop():
    net.send("desktop", "lock")

# On the desktop
@net.on("lock")
def lock():
    run("rundll32.exe user32.dll,LockWorkStation")

@net.on("notify")
def notify(text):
    ui.toast(f"{net.sender()}: {text}")
```

First pair the machines: on one, choose **Networking > Generate a pairing token** from the tray menu and copy the token it shows; on each of the others choose **Networking > Enter a pairing token** and paste it.  Only machines with the same token talk to each other, and nothing listens for messages until a token is set.

Machines find each other by name, which is the computer's name unless the `net_name` setting gives another: through Tailscale's MagicDNS or ordinary DNS, or through Bonjour on the local network.  There's no relay server, so the machines have to share a network or a VPN such as Tailscale.  Messages are signed with the token, so they can't be forged or changed on the way, but they aren't secret; Tailscale encrypts them.  The first time MicroKeys takes messages, Windows asks whether to let it through the firewall.

## Keeping data between runs

`store` is a dict that MicroKeys saves after every macro and loads again when it starts.  It keeps anything JSON can: strings, numbers, `True`, `False`, `None`, and lists and dicts of those.

```python
from microkeys import hotkey, store, keys

@hotkey("ctrl+alt+n")
def next_ticket():
    store["ticket"] = store.get("ticket", 1000) + 1
    keys.type(f"TICKET-{store['ticket']}")
```

It's saved as `store.json` in `%APPDATA%\MicroKeys`.

## Showing what a macro is doing

`ui.show()` puts a short message near the bottom of the screen, above every other window.  It never takes the focus, so the keys a macro sends still reach the window they're meant for, and clicks go through it.

```python
from microkeys import hotkey, clip, ui, windows

@hotkey("ctrl+alt+w")
def copy_title():
    clip.set(windows.active().title)
    ui.show("Copied the window title")
```

A message stays up while the macro that showed it runs and for two seconds after it finishes, so a long macro can show how far it's got with a `ui.show()` at each step.  `ui.show(text, seconds=5)` keeps a message up for five seconds instead, even after the macro is done; `seconds=math.inf` keeps it up until `ui.hide()`.  Each `ui.show()` replaces the message before it.

`ui.toast()` sends a notification instead, which stays in the notification center, and `ui.msgbox()` waits for an answer.

## Stopping a macro

Press **Ctrl+Alt+Backspace** to stop the macro that's running, or use **Stop running macro** in the tray menu.  While a macro runs for more than a moment the tray icon shows an orange dot.

Macros run one at a time.  If you press another hotkey while one runs, it waits its turn.

**Pause** in the tray menu turns off hotkeys, hotstrings, remapped keys, timers, events and messages until you choose it again.

## Moving to another computer

**Export scripts and settings...** in the tray menu saves your scripts, your settings and the `store` in one zip file.  On the other computer, **Import scripts and settings...** shows what would change, then either merges the zip in (its files and settings win, everything else stays) or replaces what's there to match it.  Before an import changes anything, MicroKeys saves what was there as a zip in `%APPDATA%\MicroKeys\backups`.  The `script_folder` setting and the networking token stay with each computer.

## Running as administrator

Windows doesn't let a program send keys or clicks to a program running as administrator unless it runs as administrator too.  **Run as administrator** in the tray menu restarts MicroKeys with administrator rights, after Windows asks you to allow it, and it starts that way from then on.  With **Start with Windows** on as well, MicroKeys sets up a scheduled task so it can start as administrator when you sign in without asking.  `Window.elevated` tells whether a window belongs to a program MicroKeys can't reach this way.  Keep in mind that programs your macros start then run as administrator too.

## Settings

**Edit settings** in the tray menu opens `settings.json` (in `%APPDATA%\MicroKeys`).  Changes take effect when the scripts next reload.

- `script_folder`: where your scripts live.  Empty means `Documents\MicroKeys`.
- `abort_hotkey`: the key that stops a macro, `"ctrl+alt+backspace"` by default.
- `key_delay_ms`: the pause between keys when typing, 5 by default.
- `key_hold_ms`: how long each key stays down when typing, 15 by default.  Raise it, or `key_delay_ms`, if a program drops characters.
- `directinput_key_ms`: in programs that use DirectInput, mostly games, the least time each key stays down and the pause after it, 40 by default.  Many of them read the keyboard once a frame and miss shorter keys; 40 covers 30 frames a second, with some room for a slow frame.  MicroKeys also types characters there with the keys that make them, since DirectInput doesn't see text.  0 turns this off.
- `directinput_programs`: programs to treat as using DirectInput or not, whatever MicroKeys detects, like `{"oldgame.exe": true, "launcher.exe": false}`.  Names can use `*` and `?`.  Use it for a game that misses keys although MicroKeys finds no DirectInput in it, or for a program that loads DirectInput but types fine at full speed.
- `check_updates`: look for a new version once a day. <!-- updates -->
- `clipboard_history`: how many copied texts `clip.history()` keeps, 25 by default.  0 turns the history off.
- `editor`: what opens a file name you click in the log, with `{file}` and `{line}` where the file and line go, like `"C:\\Program Files\\Notepad++\\notepad++.exe -n{line} {file}"`.  Empty means VS Code or Notepad++ if one is installed, otherwise Notepad.
- `packages`: packages from PyPI that your scripts import, like `["requests"]`.  See [Python packages](#python-packages).
- `debug_port`: a port for a debugger to attach to, 0 (off) by default.  See [Debugging](#debugging).
- `run_elevated`: run as administrator, which **Run as administrator** in the tray menu turns on and off.  See [Running as administrator](#running-as-administrator).
- `net_name`: this computer's name for messages from other machines.  Empty means its computer name.
- `net_port`: the port other machines send messages to, 47913 by default.
- `net_listen`: where to take messages from: empty for every network, or one of this computer's addresses, such as its Tailscale address, to take them from that network only.

The log is kept in `%LOCALAPPDATA%\MicroKeys\MicroKeys.log`.

## The log and the console

**Show log** in the tray menu opens the log window.  Errors are in red, and a file name with a line number, in a traceback or a message about a script, is a link: click it to open the file at that line.

The line at the bottom of the log window is a Python prompt, like Python's own.  Type an expression or a statement and press Enter; it runs like a macro, so the abort hotkey stops it.  Everything from `microkeys` is already imported, and each script is there under its own name (unless that name is already taken, by `clip` say), so `MicroKeys.type_date()` runs the `type_date` function in `MicroKeys.py`.  A line ending in `:` starts a block, which runs when you enter an empty line.  Up and Down bring back earlier lines.

## Python packages

Scripts can use packages from PyPI.  List them in the `packages` setting, or choose **Install Python packages...** in the tray menu, which installs them and adds them to the setting:

```json
"packages": ["requests", "openpyxl"]
```

MicroKeys installs any that are missing when it starts or the scripts reload, using pip with the Python that comes with MicroKeys, into `%LOCALAPPDATA%\MicroKeys\packages`.  Only packages that come ready-built (as wheels) can be installed, since there's no compiler to build anything else; almost every popular package does.
When an update brings a new version of Python, MicroKeys installs the packages again for it. <!-- updates -->

## Debugging

To step through a macro in VS Code, set `debug_port` to a port such as `5678` and reload.  MicroKeys installs the `debugpy` package and listens on that port, on this computer only.  Then attach from VS Code with a `launch.json` configuration like this one, set breakpoints in your scripts, and press the hotkey:

```json
{
    "name": "Attach to MicroKeys",
    "type": "debugpy",
    "request": "attach",
    "connect": { "host": "127.0.0.1", "port": 5678 }
}
```

The debugger keeps listening until MicroKeys exits.

## Editor support

MicroKeys writes a `pyrightconfig.json` into your script folder that points at its copy of the `microkeys` package, so VS Code (with Pylance) and other editors that use Pyright autocomplete the API and check your calls.  Open the script folder itself as the workspace.

For other editors, or to import your scripts in a Python of your own (to test the logic in them, say), install the package into that Python:

```
python -m pip install -e "%LOCALAPPDATA%\MicroKeys\stubs"
```

MicroKeys keeps that folder up to date as it updates, and the install follows along. <!-- updates -->
Outside MicroKeys every function is there, with its documentation, but nothing reaches the desktop: each call says what it would have done instead, for example `[microkeys] type 'Hello'`, and hotkeys and timers aren't bound.

## Python

MicroKeys includes Python 3.14 with the full standard library: `json`, `csv`, `re`, `datetime`, `pathlib`, `subprocess`, `urllib`, `sqlite3`, and the rest.  Scripts can import helper modules from the script folder.

## Things to know

- Windows doesn't let a program send keys or clicks to a program running as administrator unless it's an administrator too.  If a macro does nothing in an elevated window, that's why; see [Running as administrator](#running-as-administrator).
- If another program already uses a hotkey, MicroKeys says so in the log and notification, and that hotkey stays with the other program.
- MicroKeys only runs one copy at a time.  Starting it again shows the running copy's log.
