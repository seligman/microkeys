"""The clipboard.

* `get` - the text on the clipboard
* `set` - put text, HTML, RTF, files, or an image on the clipboard
* `get_html` / `get_rtf` / `get_files` / `get_image` - other kinds of data on it
* `formats` - what kinds of data it holds
* `copy_selection` - copy what's selected in the active program and return it
* `paste_text` - paste text into the active program, then put the clipboard back
* `save` / `restore` - keep everything on the clipboard and put it back later
* `history` - text copied recently
* `on_change` - run a function when the clipboard changes
* `wait_for_change` - wait for something new on the clipboard
"""

import os
import re
from html import unescape as _unescape

from ._native import native
from . import keys, _keyspec, _bindings

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

_history = []


def get():
    """The text on the clipboard, or "" when it holds no text."""
    ret = native.clip_get()
    return "" if ret is None else ret


def _html_to_text(markup):
    # A plain text version for programs that don't take HTML: tags dropped,
    # entities decoded, line breaks where blocks end
    text = re.sub(r"(?i)<br\s*/?>|</(p|div|li|tr|h[1-6])\s*>", "\n", markup)
    text = re.sub(r"(?s)<!--.*?-->|<[^>]*>", "", text)
    return _unescape(text).strip()


def _rtf_bytes(rtf):
    if isinstance(rtf, bytes):
        return rtf
    try:
        return str(rtf).encode("latin-1")
    except UnicodeEncodeError:
        raise ValueError("RTF can only hold Latin-1 characters directly; write others as \\uN? escapes") from None


def set(text=None, *, html=None, rtf=None, files=None, image=None):
    """Put text, or other kinds of data, on the clipboard.  Returns False if
    the clipboard was busy.

        clip.set("Hello")
        clip.set(html="<b>Hello</b>")       # pastes bold where HTML is understood
        clip.set(files=[r"C:\\notes.txt"])   # paste in Explorer copies the file
        clip.set(image=png_bytes)

    Everything given goes on the clipboard together, replacing what was
    there.  With `html` and no `text`, a plain text version is made from the
    HTML for programs that only take text.  `image` is PNG data.
    """
    if text is None and html is not None:
        text = _html_to_text(str(html))
    if html is None and rtf is None and files is None and image is None:
        if text is None:
            raise ValueError("clip.set() needs something to put on the clipboard")
        return native.clip_set(str(text))
    items = []
    if text is not None:
        items.append(("text", str(text).encode("utf-8")))
    if html is not None:
        items.append(("html", str(html).encode("utf-8")))
    if rtf is not None:
        items.append(("rtf", _rtf_bytes(rtf)))
    if files is not None:
        if isinstance(files, (str, bytes, os.PathLike)):
            files = [files]
        items.append(("files", "\0".join(os.path.abspath(os.fsdecode(p)) for p in files).encode("utf-8")))
    if image is not None:
        image = bytes(image)
        if not image.startswith(PNG_SIGNATURE):
            raise ValueError("image= takes PNG data")
        items.append(("image", image))
    return native.clip_set_data(items)


def get_html():
    """The HTML on the clipboard (just the part that was copied), or None."""
    data = native.clip_get_data("html")
    return None if data is None else data.decode("utf-8", "replace")


def get_rtf():
    """The RTF (rich text, from Word or WordPad for example) on the clipboard, or None."""
    data = native.clip_get_data("rtf")
    return None if data is None else data.decode("latin-1")


def get_files():
    """The files and folders on the clipboard, as full paths, for example
    after Ctrl+C in Explorer.  [] when there are none."""
    data = native.clip_get_data("files")
    return [] if not data else [p for p in data.decode("utf-8").split("\0") if p]


def get_image():
    """The image on the clipboard as PNG data, or None.

        png = clip.get_image()
        if png:
            pathlib.Path("clip.png").write_bytes(png)
    """
    return native.clip_get_data("image")


def formats():
    """What the clipboard holds: a list with any of "text", "html", "rtf",
    "files", and "image"."""
    return native.clip_kinds()


class Saved:
    """Everything that was on the clipboard, from `save()`."""

    def __init__(self, items):
        self._items = items

    def __repr__(self):
        return f"<clip.Saved, {len(self._items)} format{'s' if len(self._items) != 1 else ''}>"


def save():
    """Take a copy of everything on the clipboard, every format, to put
    back later with `restore()`.

        saved = clip.save()
        clip.set("for a moment")
        ...
        clip.restore(saved)
    """
    return Saved(native.clip_save() or [])


def restore(saved):
    """Put back what `save()` copied.  Returns False if the clipboard was busy."""
    if not isinstance(saved, Saved):
        raise TypeError("restore() takes what save() returned")
    return native.clip_restore(saved._items)


def wait_for_change(timeout=1.0, since=None):
    """Wait for the clipboard to change.  True if it did within `timeout` seconds.

    `since` is a value from `sequence()` taken before the action expected to
    change the clipboard; by default it's taken now.
    """
    start = native.clip_sequence() if since is None else since
    waited = 0.0
    while waited < timeout:
        if native.clip_sequence() != start:
            return True
        native.sleep(0.01)
        waited += 0.01
    return native.clip_sequence() != start


def sequence():
    """A number that changes every time the clipboard does."""
    return native.clip_sequence()


def copy_selection(timeout=1.0):
    """Copy the selection in the active program and return it as text.

    Presses Ctrl+C (Cmd+C on macOS) and waits for the clipboard to change.
    Returns "" if nothing was copied within `timeout` seconds.
    """
    before = native.clip_sequence()
    keys.press(f"{_keyspec.primary()}+c")
    if not wait_for_change(timeout, since=before):
        return ""
    return get()


def paste_text(text, restore=True):
    """Paste text into the active program through the clipboard.

    Much faster than `keys.type()` for long text.  With `restore`, whatever
    was on the clipboard before, images and all, is put back afterwards.
    """
    saved = native.clip_save() if restore else None
    native.clip_set(str(text))
    keys.press(f"{_keyspec.primary()}+v")
    if saved is not None:
        # Give the program time to read the clipboard before it changes back
        native.sleep(0.3)
        native.clip_restore(saved)


def history():
    """Text copied recently, newest first, without repeats.

    MicroKeys keeps the last 25 (the clipboard_history setting; 0 turns it
    off), in memory only.  It leaves out what MicroKeys itself puts on the
    clipboard, and what password managers mark as private.
    """
    return list(_history)


def clear_history():
    """Forget the clipboard history."""
    _history.clear()


def record(limit):
    # The clipboard changed: note its text in the history
    if limit <= 0:
        _history.clear()
        return
    if native.clip_concealed():
        return
    text = native.clip_get()
    if not text or not text.strip():
        return
    if text in _history:
        _history.remove(text)
    _history.insert(0, text)
    del _history[limit:]


def on_change(func=None):
    """Run a function when another program changes the clipboard.

        @clip.on_change
        def copied():
            log("Copied:", clip.get()[:40])

    Changes MicroKeys makes itself, with `set()` or `paste_text()`, don't
    count.  Several changes while a macro runs may count as one.
    """
    return _bindings.handler("on_change", "clipboard", func)
