"""The clipboard.

* `get` - the text on the clipboard
* `set` - put text on the clipboard
* `copy_selection` - copy what's selected in the active program and return it
* `paste_text` - paste text into the active program, then put the clipboard back
* `wait_for_change` - wait for something new on the clipboard
"""

from ._native import native
from . import keys, _keyspec


def get():
    """The text on the clipboard, or "" when it holds no text."""
    ret = native.clip_get()
    return "" if ret is None else ret


def set(text):
    """Put text on the clipboard.  Returns False if the clipboard was busy."""
    return native.clip_set(str(text))


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

    Much faster than `keys.type()` for long text.  With `restore`, the text
    that was on the clipboard before is put back afterwards.
    """
    old = native.clip_get() if restore else None
    native.clip_set(str(text))
    keys.press(f"{_keyspec.primary()}+v")
    if restore and old is not None:
        # Give the program time to read the clipboard before it changes back
        native.sleep(0.3)
        native.clip_set(old)
