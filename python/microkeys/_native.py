# Access to the _microkeys module that the MicroKeys app builds in.  Outside
# the app (plain Python, an editor, a docs tool) the stand-in in _offline
# takes its place and says what each call would have done.

LOG_INFO = 0
LOG_OUTPUT = 1
LOG_ERROR = 2
LOG_DEBUG = 3

MOD_CTRL = 1
MOD_ALT = 2
MOD_SHIFT = 4
MOD_META = 8

FIRE_HOTKEY = 1
FIRE_HOTSTRING = 2
FIRE_COMMAND = 3
FIRE_WINDOW = 4
FIRE_TIMER = 5
FIRE_CLIPBOARD = 6

WINDOW_ACTIVATE = 1
WINDOW_OPEN = 2

try:
    import _microkeys as native
except ImportError:
    from . import _offline as native

OFFLINE = getattr(native, "OFFLINE", False)
