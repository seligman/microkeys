# Cookbook

Short macros to copy into your scripts and adapt.  Each assumes the imports at the top of its block.

## Change the selected text

Copy the selection, change it in Python, and type the result over it.

```python
from microkeys import hotkey, clip, keys

@hotkey("ctrl+shift+u")
def uppercase_selection():
    text = clip.copy_selection()
    if text:
        keys.type(text.upper())

@hotkey("ctrl+shift+s")
def sort_selected_lines():
    text = clip.copy_selection()
    lines = sorted(text.splitlines(), key=str.lower)
    clip.paste_text("\n".join(lines))
```

`paste_text()` is much faster than typing for long text, and puts the old clipboard back afterwards.

## Calculator anywhere

```python
from microkeys import hotkey, clip, keys

@hotkey("ctrl+alt+e")
def evaluate():
    text = clip.copy_selection()
    if text:
        keys.type(f"{text} = {eval(text)}")
```

## Switch to a program, or start it

```python
from microkeys import hotkey, windows, run

@hotkey("ctrl+alt+n")
def notepad():
    win = windows.find_one(process="notepad.exe")
    if win:
        win.activate()
    else:
        run("notepad.exe")
```

## Arrange windows

```python
from microkeys import hotkey, windows

@hotkey("ctrl+alt+left")
def left_half():
    win = windows.active()
    mon = windows.monitors()[win.monitor]
    win.move(mon.work_x, mon.work_y, mon.work_width // 2, mon.work_height)

@hotkey("ctrl+alt+right")
def right_half():
    win = windows.active()
    mon = windows.monitors()[win.monitor]
    half = mon.work_width // 2
    win.move(mon.work_x + half, mon.work_y, mon.work_width - half, mon.work_height)

@hotkey("ctrl+alt+m")
def next_monitor():
    win = windows.active()
    win.move_to_monitor(win.monitor + 1)
```

## Fill in a template

```python
from microkeys import hotkey, keys, ui

@hotkey("ctrl+alt+r")
def reply():
    name = ui.input("Reply to whom?")
    if name:
        keys.type(f"Hi {name},\n\nThanks for getting in touch.\n\n")

@hotkey("ctrl+alt+g")
def greeting():
    choice = ui.choose(["Good morning", "Good afternoon", "Good evening"], prompt="Which greeting?")
    if choice:
        keys.type(choice + ", ")
```

## Text expansion with a function

```python
import datetime
from microkeys import hotstring, keys

@hotstring(";week")
def week_number():
    keys.type(f"week {datetime.date.today().isocalendar().week}")

hotstring("teh", "the", immediate=False)
hotstring("recieve", "receive", immediate=False)
```

## Open the selected text as a web search

```python
import urllib.parse
import microkeys
from microkeys import hotkey, clip

@hotkey("ctrl+alt+s")
def search():
    text = clip.copy_selection().strip()
    if text:
        microkeys.open("https://www.google.com/search?q=" + urllib.parse.quote(text))
```

## Keys for one program only

```python
from microkeys import hotkey, keys, windows

EXCEL = windows.match(process="excel.exe")

@hotkey("f9", when=EXCEL)
def excel_today():
    keys.press("ctrl+;")
```

## A command for a desktop shortcut

```python
from microkeys import command, windows

@command("minimize-all")
def minimize_all():
    for win in windows.all():
        win.minimize()
```

Create a shortcut to `MicroKeys.exe run minimize-all`.

## Click something

Coordinates are screen pixels.  `mouse.position()` in a quick test macro tells you where something is.

```python
from microkeys import hotkey, mouse, sleep

@hotkey("ctrl+alt+k")
def click_twice():
    x, y = mouse.position()
    mouse.click()
    sleep(0.5)
    mouse.click(x=x + 100, y=y)
```

## Hold a key down

```python
from microkeys import hotkey, keys

@hotkey("ctrl+alt+down")
def select_five_lines():
    with keys.hold("shift"):
        keys.press(*["down"] * 5)
```
