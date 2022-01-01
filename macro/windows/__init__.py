# This is a mock version of the clip implementation in MicroKeys
# It's only here so editors like VS Code will have autocomplete

"""Window helper functions

This module provides helper functions for interacting with the
windows.

* `list_all` - List all visible windows
* `get_active` - Returns the active window
* `set_active` - Moves a window to the foreground
"""

from typing import List, Tuple


def list_all() -> List[Tuple[int, str, str]]:
    """List all windows.
    
    Returns a list of tuples containing a handle to the window, the current 
    window title, and the window class name.
    
    Only shows windows that are visible.
    """
    return [(0, "Example window", "ClassName")]

def get_active() -> Tuple[int, str, str]:
    """Returns the active window.
    
    Returns a tuple with the handle to the window, title of the window, and
    the window title that currently has focus.
    """
    return (0, "Example window", "ClassName")

def set_active(handle: int):
    """Brings a window to the front.
    
    Brings a given window to the foreground, restoring it from a minimized
    state if necessary
    """
    print(f"windows.set_active({handle})")
