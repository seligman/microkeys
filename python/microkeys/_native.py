# Access to the _microkeys module that the MicroKeys app builds in.  Outside
# the app (an editor, a docs tool) it's missing, and any use says so.

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


class _Missing:
    def __getattr__(self, name):
        raise RuntimeError("The microkeys package only works inside MicroKeys")


try:
    import _microkeys as native
except ImportError:
    native = _Missing()
