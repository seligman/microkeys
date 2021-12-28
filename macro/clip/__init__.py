# This is a mock version of the clip implementation in MicroKeys
# It's only here so editors like VS Code will have autocomplete

"""Clipboard helper functions

This module provides helper functions for interacting with the
clipboard.

* `copy` - Returns the contents of the clipboard
* `paste` - Places a string in the clipboard
"""

def copy() -> str:
    """Returns the current contents of the clipboard."""
    return "<clipboard>"

def paste(value):
    """Empties the clipboard and places the text value in it."""
    print(f"To clipboard: {value}")
