"""The mouse.

Coordinates are screen pixels, with (0, 0) at the top left of the main
monitor.  Monitors to its left or above it have negative coordinates.

* `position` - where the pointer is
* `move` / `move_by` - move the pointer
* `click`, `down`, `up` - mouse buttons: "left", "right", "middle", "x1", "x2"
* `scroll` - turn the wheel
* `drag` - press, move, and release
"""

from ._native import native

BUTTONS = ["left", "right", "middle", "x1", "x2"]


def position():
    """The pointer's position as (x, y)."""
    return native.cursor()


def move(x, y):
    """Move the pointer to (x, y)."""
    native.move_cursor(int(x), int(y))


def move_by(dx, dy):
    """Move the pointer by (dx, dy) from where it is."""
    x, y = native.cursor()
    native.move_cursor(x + int(dx), y + int(dy))


def down(button="left"):
    """Press a button and keep it down."""
    native.mouse_button(button, True)


def up(button="left"):
    """Let a button go."""
    native.mouse_button(button, False)


def click(button="left", count=1, x=None, y=None):
    """Click a button `count` times, optionally moving to (x, y) first."""
    if x is not None and y is not None:
        move(x, y)
    for i in range(count):
        native.mouse_button(button, True)
        native.mouse_button(button, False)


def scroll(clicks=1, horizontal=False):
    """Turn the wheel: positive is up (or right), negative is down (or left)."""
    native.mouse_wheel(int(clicks), bool(horizontal))


def drag(x1, y1, x2, y2, button="left"):
    """Press at (x1, y1), move to (x2, y2), and let go."""
    move(x1, y1)
    native.mouse_button(button, True)
    try:
        native.sleep(0.05)
        move(x2, y2)
        native.sleep(0.05)
    finally:
        native.mouse_button(button, False)
