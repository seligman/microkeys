"""Talking to the user.

* `msgbox` - show a message, optionally with OK/Cancel or Yes/No
* `input` - ask for a line of text
* `choose` - pick one item from a list
* `toast` - a notification that doesn't need an answer
"""

from ._native import native

BUTTONS = ["ok", "okcancel", "yesno", "yesnocancel"]


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


def toast(text, title="MicroKeys"):
    """Show a notification in the corner of the screen."""
    native.toast(str(title), str(text))
