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

## Pick from the clipboard history

```python
from microkeys import hotkey, clip, ui

@hotkey("ctrl+alt+h")
def paste_from_history():
    items = clip.history()
    labels = [f"{i + 1}. " + text.replace("\n", " ")[:80] for i, text in enumerate(items)]
    choice = ui.choose(labels, prompt="Paste which?")
    if choice is not None:
        clip.paste_text(items[labels.index(choice)])
```

## Paste without formatting

```python
from microkeys import hotkey, clip

@hotkey("ctrl+shift+v")
def paste_plain():
    clip.paste_text(clip.get())
```

## Remind me later

```python
from microkeys import hotkey, after, ui

@hotkey("ctrl+alt+t")
def remind():
    text = ui.input("Remind you of what, in 10 minutes?")
    if text:
        after(10 * 60, lambda: ui.msgbox(text, title="Reminder"))
```

## Show progress

```python
import urllib.request
from pathlib import Path
from microkeys import hotkey, clip, ui

@hotkey("ctrl+alt+g")
def download_links():
    # The links on the clipboard, one per line, saved in Downloads
    urls = (clip.get() or "").split()
    for i, url in enumerate(urls, 1):
        ui.show(f"Downloading {i} of {len(urls)}\n{url}")
        name = url.rstrip("/").rsplit("/", 1)[-1] or "download"
        urllib.request.urlretrieve(url, Path.home() / "Downloads" / name)
    ui.show(f"Downloaded {len(urls)} files")
```

## Show when a mode is on

```python
import math
from microkeys import hotkey, unbind, keys, ui

@hotkey("ctrl+alt+j")
def arrow_keys_on():
    hotkey("j", lambda: keys.press("down"))
    hotkey("k", lambda: keys.press("up"))
    hotkey("escape", arrow_keys_off)
    ui.show("J and K are arrow keys (Escape to stop)", seconds=math.inf)

def arrow_keys_off():
    for key in ("j", "k", "escape"):
        unbind(hotkey=key)
    ui.hide()
```

## Count something, across restarts

```python
from microkeys import hotkey, store, ui

@hotkey("ctrl+alt+c")
def one_more():
    store["coffees"] = store.get("coffees", 0) + 1
    ui.toast(f"Coffee number {store['coffees']}")
```

## Fill in a dialog when it opens

```python
from microkeys import windows, keys

@windows.on_open(when=windows.match(process="notepad.exe", title="Save As"))
def name_the_file(win):
    keys.type("notes.txt")
```

## Save an image from the clipboard

```python
import datetime, os
import microkeys
from microkeys import hotkey, clip, ui

@hotkey("ctrl+alt+i")
def save_clipboard_image():
    png = clip.get_image()
    if png is None:
        ui.toast("There's no image on the clipboard")
        return
    fn = os.path.expanduser(datetime.datetime.now().strftime("~/Pictures/clip-%Y%m%d-%H%M%S.png"))
    with open(fn, "wb") as f:
        f.write(png)
    microkeys.open(fn)
```

## Use a package from PyPI

Add `"packages": ["requests"]` to your settings, or choose **Install Python packages...** in the tray menu, and then:

```python
import requests
from microkeys import hotkey, keys

@hotkey("ctrl+alt+w")
def type_my_ip():
    keys.type(requests.get("https://api.ipify.org", timeout=10).text)
```

## Caps Lock as another Ctrl

```python
from microkeys import keys

keys.remap("capslock", "ctrl")
```

## Mouse side buttons for back and forward

```python
from microkeys import hotkey, keys

@hotkey("mousex1", when="code.exe")
def go_back():
    keys.press("alt+left")

@hotkey("mousex2", when="code.exe")
def go_forward():
    keys.press("alt+right")
```

## Click a button by its picture

Save a small screenshot of the button first, for example from the log window's prompt with `screen.capture((x, y, width, height)).save("C:\\pics\\connect.png")`.

```python
from microkeys import hotkey, screen, ui

@hotkey("ctrl+alt+k")
def connect():
    found = screen.wait_for(r"C:\pics\connect.png", timeout=5, tolerance=20)
    if found is None:
        ui.toast("No Connect button on screen")
        return
    found.click()
```

## Click a link by what it says

No picture needed: the text is found in the page (or read off the screen), wherever it's scrolled to.

```python
from microkeys import hotkey, screen, ui

@hotkey("ctrl+alt+n")
def next_page():
    found = screen.wait_for_text("Next page", timeout=5)
    if found is None:
        ui.toast("No 'Next page' on screen")
        return
    found.click()
```

## Copy the text out of a picture

OCR reads text from anything on screen, such as a screenshot in a chat, a video, or a scanned page.  Point at the top left of the area, press the hotkey, point at the bottom right, and press it again.

```python
from microkeys import hotkey, screen, mouse, clip, ui

corner = []

@hotkey("ctrl+alt+t")
def grab_text():
    corner.append(mouse.position())
    if len(corner) < 2:
        ui.show("Now point at the bottom right and press it again", seconds=3)
        return
    (x1, y1), (x2, y2) = corner
    corner.clear()
    lines = screen.read_text((min(x1, x2), min(y1, y2), abs(x2 - x1), abs(y2 - y1)))
    clip.set("\n".join(line.text for line in lines))
    ui.show(f"Copied {len(lines)} lines", seconds=2)
```

## Tick a box and press a button

Controls are found by what they say, so this works wherever the dialog is.

```python
from microkeys import windows, controls

@windows.on_open(when=windows.match(title="Terms of Service"))
def accept(win):
    controls.find(name="I agree*", role="checkbox", window=win).click()
    controls.find(name="Continue", role="button", window=win).click()
```

## Send the selection to another computer

Pair the machines first (**Networking** in the tray menu).

```python
from microkeys import hotkey, clip, net, ui

# On the laptop
@hotkey("ctrl+alt+c")
def send_selection():
    net.send("desktop", "clipboard", clip.copy_selection())

# On the desktop
@net.on("clipboard")
def take_clipboard(text):
    clip.set(text)
    ui.show(f"Clipboard from {net.sender()}")
```
