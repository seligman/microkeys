# This is a mock version of the keys implementation in MicroKeys
# It's only here so editors like VS Code will have autocomplete

"""Mouse manipulation

This module provides mouse interactions:

* `position` - Return the current mouse position
* `move` - Move the mouse cursor to an absolute position
* `offset` - Move the mouse cursor from its current position
* `click` - Click the left mouse button
* `right_click` - Click the right mouse button
* `down` - Press the left mouse button down
* `right_down` - Press the right mouse button down
* `up` - Release the left mouse button
* `right_up` - Release the right mouse button

"""

from typing import Tuple

def position() -> Tuple[int, int]:
    """Returns the current mouse position"""
    print(f"mouse.position()")
    return (0, 0)

def move(x: int, y: int) -> None:
    """Moves the mouse to an absolute x, y position"""
    print(f"mouse.move({x}, {y})")

def offset(x: int, y: int) -> None:
    """Moves the mouse to an x, y offset from the current position"""
    print(f"mouse.offset({x}, {y})")

def click() -> None:
    """Clicks the left mouse button"""
    print("mouse.click()")

def right_click() -> None:
    """Clicks the right mouse button"""
    print("mouse.right_click()")

def down() -> None:
    """Presses the left mouse button down"""
    print("mouse.click()")

def right_down() -> None:
    """Presses the right mouse button down"""
    print("mouse.right_down()")

def up() -> None:
    """Releases the left mouse button"""
    print("mouse.up()")

def right_up() -> None:
    """Releases the right mouse button"""
    print("mouse.right_up()")

