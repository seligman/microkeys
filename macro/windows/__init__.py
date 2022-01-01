# This is a mock version of the clip implementation in MicroKeys
# It's only here so editors like VS Code will have autocomplete

"""Window helper functions

This module provides helper functions for interacting with the
windows.

* `launch` - Launches a program
* `list_all` - List all visible windows
* `get_active` - Returns the active window
* `set_active` - Moves a window to the foreground
* `get_position` - Get the window position of a window
* `set_position` - Set the window position for a window
"""

from typing import List, Tuple


def list_all() -> List[Tuple[str, str, str]]:
    """List all windows.
    
    Returns a list of tuples containing a handle to the window, the current 
    window title, and the window class name.
    
    Only shows windows that are visible.
    """
    return [(0, "Example window", "ClassName")]

def get_active() -> Tuple[str, str, str]:
    """Returns the active window.
    
    Returns a tuple with the handle to the window, title of the window, and
    the window title that currently has focus.
    """
    return (0, "Example window", "ClassName")

def set_active(handle: str):
    """Brings a window to the front.
    
    Brings a given window to the foreground, restoring it from a minimized
    state if necessary
    """
    print(f"windows.set_active('{handle}')")

def get_position(handle: str) -> Tuple[int, int, int, int]:
    """Gets the window location and size of a given window.
    
    Returns a 4 element tuple containing the x and y position along
    with the width and height.
    """
    return (0, 0, 100, 100)

def set_position(handle: str, x: int, y: int, width: int, height: int):
    """Moves and resizes a window to a given location."""
    print(f"window.set_position('{handle}', {x}, {y}, {width}, {height})")

def launch(command_line: str):
    """Launch an executable, with an optional command line to pass.
    
    Note that the string is passed as is, so if the path to the 
    executable contains spaces, it must be quoted.
    """
    print(f"windows.launch('{command_line}')")
