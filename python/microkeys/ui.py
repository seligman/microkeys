"""Talking to the user.

* `msgbox` - show a message, optionally with OK/Cancel or Yes/No
* `input` - ask for a line of text
* `choose` - pick one item from a list
* `choose_file` - pick a file to open or save to
* `toast` - a notification that doesn't need an answer
* `show` - put a message on screen while a macro works, or to say it's done
* `hide` - take that message down
"""

from ._native import native

BUTTONS = ["ok", "okcancel", "yesno", "yesnocancel"]

# How long a message stays up after the macro that showed it finishes
LINGER = 2.0
# The message to keep up until the current macro finishes, if any
_lasting = None


def msgbox(text, title="MicroKeys", buttons="ok"):
    """Show a message and wait for an answer.

    `buttons` is "ok", "okcancel", "yesno", or "yesnocancel".  Returns the
    button pressed: "ok", "cancel", "yes", or "no".
    """
    if buttons not in BUTTONS:
        raise ValueError(f"buttons must be one of {', '.join(BUTTONS)}")
    return native.msgbox(str(text), str(title), buttons)


def input(prompt, title="MicroKeys", default=""):
    """Ask for a line of text.  Returns None if the user cancels."""
    return native.ask_text(str(prompt), str(title), str(default))


def choose(items, prompt="", title="MicroKeys"):
    """Let the user pick one of `items`.  Returns the item, or None.

    Items can be anything; they're shown with str().
    """
    items = list(items)
    if not items:
        return None
    index = native.ask_choice([str(x) for x in items], str(prompt), str(title))
    return items[index] if 0 <= index < len(items) else None


def choose_file(save=False, title=None, types=None, default=""):
    """Ask for a file to open or, with `save`, a file to save to.  Returns
    its full path, or None if the user cancels.

        path = ui.choose_file(types=[("Text files", "*.txt"), ("All files", "*.*")])
        path = ui.choose_file(save=True, default="report.csv")

    `types` is a list of (description, pattern) pairs, and a pattern can
    hold several, like "*.jpg;*.png".  When saving, a name typed without an
    extension gets the first type's, and the user is asked before an
    existing file is replaced.  `default` is the file name, or full path, to
    start with.
    """
    pairs = []
    for item in types or []:
        name, pattern = item
        pairs.append((str(name), str(pattern)))
    if title is None:
        title = "Save as" if save else "Open"
    return native.ask_file(bool(save), str(title), pairs, str(default or ""))


def toast(text, title="MicroKeys"):
    """Show a notification in the corner of the screen."""
    native.toast(str(title), str(text))


def show(text, seconds=None):
    """Put a message on screen, above every other window.

        ui.show("Copied")

        for i, name in enumerate(names, 1):
            ui.show(f"Renaming {i} of {len(names)}\n{name}")
            ...

    The message sits near the bottom of the screen you're working on.  It
    never takes the focus, so keys a macro sends still reach the window
    they're meant for, and clicks go through it to whatever is underneath.

    With no `seconds`, it stays up while the macro that showed it runs and
    for two seconds after it finishes.  With `seconds`, it stays that long,
    even after the macro is done; `seconds=math.inf` keeps it up until
    hide().  Each show() replaces the message before it.
    """
    global _lasting
    text = str(text)
    if not text:
        hide()
        return
    if seconds is None:
        _lasting = text
        native.overlay(text, -1)
        return
    seconds = float(seconds)
    if not seconds > 0:
        raise ValueError("show() needs seconds greater than 0")
    _lasting = None
    native.overlay(text, int(min(seconds * 1000, 0x7FFFFFFF)))


def hide():
    """Take down the message show() put up."""
    global _lasting
    _lasting = None
    native.overlay("", 0)


def _macro_done():
    # The macro that showed a message without `seconds` has finished
    global _lasting
    if _lasting is not None:
        native.overlay(_lasting, int(LINGER * 1000))
        _lasting = None
