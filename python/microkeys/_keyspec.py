# Key names and key combinations like "ctrl+alt+z".  Names are the same on
# every platform; each platform maps them to its own key codes.

from ._native import native, MOD_CTRL, MOD_ALT, MOD_SHIFT, MOD_META

MODIFIERS = {
    "ctrl": MOD_CTRL,
    "control": MOD_CTRL,
    "alt": MOD_ALT,
    "option": MOD_ALT,
    "shift": MOD_SHIFT,
    "meta": MOD_META,
    "win": MOD_META,
    "windows": MOD_META,
    "cmd": MOD_META,
    "command": MOD_META,
    "super": MOD_META,
}

# Modifiers for one side of the keyboard.  They can be sent, but a hotkey
# can't tell the sides apart, so in a hotkey they mean the plain modifier.
SIDED_MODIFIERS = {
    "lctrl": MOD_CTRL,
    "rctrl": MOD_CTRL,
    "lalt": MOD_ALT,
    "ralt": MOD_ALT,
    "lshift": MOD_SHIFT,
    "rshift": MOD_SHIFT,
    "lmeta": MOD_META,
    "rmeta": MOD_META,
    "lwin": MOD_META,
    "rwin": MOD_META,
}

ALIASES = {
    "control": "ctrl",
    "option": "alt",
    "win": "meta",
    "windows": "meta",
    "cmd": "meta",
    "command": "meta",
    "super": "meta",
    "lwin": "lmeta",
    "rwin": "rmeta",
    "return": "enter",
    "esc": "escape",
    "del": "delete",
    "ins": "insert",
    "pgup": "pageup",
    "pgdn": "pagedown",
    "bs": "backspace",
    "bksp": "backspace",
    "caps": "capslock",
    "spacebar": "space",
    "prtsc": "printscreen",
    "break": "pause",
    "menu": "apps",
}

MOD_ORDER = [(MOD_CTRL, "ctrl"), (MOD_ALT, "alt"), (MOD_SHIFT, "shift"), (MOD_META, "meta")]

# Every named key MicroKeys knows, besides single characters like "a" or "/"
NAMED_KEYS = (
    ["ctrl", "alt", "shift", "meta"]
    + sorted(set(ALIASES[x] if x in ALIASES else x for x in SIDED_MODIFIERS))
    + ["enter", "tab", "space", "backspace", "escape", "insert", "delete", "home", "end",
       "pageup", "pagedown", "up", "down", "left", "right", "capslock", "numlock",
       "scrolllock", "printscreen", "pause", "apps"]
    + [f"f{i}" for i in range(1, 25)]
    + [f"numpad{i}" for i in range(10)]
    + ["numpadadd", "numpadsubtract", "numpadmultiply", "numpaddivide", "numpaddecimal", "numpadenter"]
    + ["volumeup", "volumedown", "volumemute", "medianext", "mediaprev", "mediaplaypause", "mediastop"]
)

_NAMED = set(NAMED_KEYS)


def primary():
    # The modifier for copy, paste, and friends: Cmd on macOS, Ctrl elsewhere
    return "meta" if native.platform() == "macos" else "ctrl"


def canonical(name):
    # The one name MicroKeys uses for a key, or ValueError
    if not isinstance(name, str) or len(name) == 0:
        raise ValueError(f"Not a key: {name!r}")
    if len(name) == 1:
        return name.lower()
    low = name.strip().lower()
    if low == "primary":
        return primary()
    low = ALIASES.get(low, low)
    if low not in _NAMED:
        raise ValueError(f"Unknown key name: {name!r}")
    return low


def is_modifier(name):
    return name in MODIFIERS or name in SIDED_MODIFIERS


def _split(text):
    # "ctrl+alt+z" -> ["ctrl", "alt", "z"], with "+" itself allowed as the
    # last key: "ctrl++" -> ["ctrl", "+"]
    text = text.strip()
    if text == "+":
        return ["+"]
    if text.endswith("++"):
        return [x.strip() for x in text[:-2].split("+")] + ["+"]
    return [x.strip() for x in text.split("+")]


def split_combo(text):
    # Every key in a combination, modifiers first, in canonical form
    if not isinstance(text, str):
        raise TypeError(f"Expected a key combination string, got {type(text).__name__}")
    parts = _split(text)
    if any(len(x) == 0 for x in parts):
        raise ValueError(f"Malformed key combination: {text!r}")
    return [canonical(x) for x in parts]


def parse_hotkey(text):
    # A hotkey: modifiers plus exactly one other key.  Returns (mods, key, description)
    keys = split_combo(text)
    mods = 0
    key = None
    for cur in keys:
        if is_modifier(cur):
            mods |= MODIFIERS.get(cur, 0) | SIDED_MODIFIERS.get(cur, 0)
        elif key is None:
            key = cur
        else:
            raise ValueError(f"A hotkey has one key besides the modifiers: {text!r}")
    if key is None:
        raise ValueError(f"A hotkey needs a key besides the modifiers: {text!r}")
    if len(key) > 1 and not native.is_key(key):
        raise ValueError(f"This platform doesn't support the {key!r} key")
    return mods, key, describe(mods, key)


def describe(mods, key):
    # "Ctrl+Alt+Z" style text for the log
    parts = [name.capitalize() for bit, name in MOD_ORDER if mods & bit]
    parts.append(key.upper() if len(key) == 1 else key.capitalize())
    return "+".join(parts)


def modifier_names(mods):
    return [name for bit, name in MOD_ORDER if mods & bit]
