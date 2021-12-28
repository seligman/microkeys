# This is a mock version of the keys implementation in MicroKeys
# It's only here so editors like VS Code will have autocomplete

"""Keyboard manipulation

This module mostly provides keyboard interactions:

* `key` - A function decoration used to register hot keys
* `KEY_xxx` - Various key definitions, such as `KEY_ALT`, `KEY_A`, and so on
* `press` - Press keys based off a string

"""

from typing import Any

KEY_0 = 0x30
KEY_1 = 0x31
KEY_2 = 0x32
KEY_3 = 0x33
KEY_4 = 0x34
KEY_5 = 0x35
KEY_6 = 0x36
KEY_7 = 0x37
KEY_8 = 0x38
KEY_9 = 0x39
KEY_A = 0x41
KEY_ALT = 0x100
KEY_B = 0x42
KEY_BACK = 0x08
KEY_C = 0x43
KEY_CLEAR = 0x0C
KEY_CONTROL = 0x200
KEY_CTRL = 0x200
KEY_D = 0x44
KEY_DEL = 0x2E
KEY_DELETE = 0x2E
KEY_DOWN = 0x28
KEY_E = 0x45
KEY_END = 0x23
KEY_ENTER = 0x0D
KEY_ESC = 0x1B
KEY_ESCAPE = 0x1B
KEY_F = 0x46
KEY_F1 = 0x70
KEY_F10 = 0x79
KEY_F11 = 0x7A
KEY_F12 = 0x7B
KEY_F2 = 0x71
KEY_F3 = 0x72
KEY_F4 = 0x73
KEY_F5 = 0x74
KEY_F6 = 0x75
KEY_F7 = 0x76
KEY_F8 = 0x77
KEY_F9 = 0x78
KEY_G = 0x47
KEY_H = 0x48
KEY_HOME = 0x24
KEY_I = 0x49
KEY_INS = 0x2D
KEY_INSERT = 0x2D
KEY_J = 0x4A
KEY_K = 0x4B
KEY_L = 0x4C
KEY_LEFT = 0x25
KEY_M = 0x4D
KEY_N = 0x4E
KEY_O = 0x4F
KEY_P = 0x50
KEY_Q = 0x51
KEY_R = 0x52
KEY_RETURN = 0x0D
KEY_RIGHT = 0x27
KEY_S = 0x53
KEY_SHIFT = 0x400
KEY_SPACE = 0x20
KEY_T = 0x54
KEY_TAB = 0x09
KEY_U = 0x55
KEY_UP = 0x26
KEY_V = 0x56
KEY_W = 0x57
KEY_WIN = 0x800
KEY_WINDOWS = 0x800
KEY_X = 0x58
KEY_Y = 0x59
KEY_Z = 0x5A

def press(keys: str) -> None:
    """Performs a series of key presses

    `keys` represents the keys to be pressed.  Right now the only 
    special key is "\\n", which will trigger a return key to be 
    pressed.
    TODO: Need to support more features
    """

    print(f"key.press('{keys}')")

def key(key_code: int) -> Any:
    """Decoration for a function to register it as a macro key

    `key_code` is a combination of KEY_xxx codes.  For instance
    `keys.KEY_ALT | keys.KEY_Z` will register Alt+Z and call the
    function when the user presses Alt+Z.
    """
    def key_internal(func):
        return func
    return key_internal
