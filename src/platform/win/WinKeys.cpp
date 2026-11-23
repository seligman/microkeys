#include "WinKeys.h"
#include "WinText.h"

class NamedKey {
public:
	const char* Name;
	UINT VK;
	bool Extended;
};

// Every key name the Python layer knows (keys.NAMED_KEYS), besides single
// characters, which go through the keyboard layout instead
static const NamedKey _namedKeys[] = {
	{ "ctrl", VK_CONTROL, false },
	{ "alt", VK_MENU, false },
	{ "shift", VK_SHIFT, false },
	{ "meta", VK_LWIN, true },
	{ "lctrl", VK_LCONTROL, false },
	{ "rctrl", VK_RCONTROL, true },
	{ "lalt", VK_LMENU, false },
	{ "ralt", VK_RMENU, true },
	{ "lshift", VK_LSHIFT, false },
	{ "rshift", VK_RSHIFT, false },
	{ "lmeta", VK_LWIN, true },
	{ "rmeta", VK_RWIN, true },
	{ "enter", VK_RETURN, false },
	{ "tab", VK_TAB, false },
	{ "space", VK_SPACE, false },
	{ "backspace", VK_BACK, false },
	{ "escape", VK_ESCAPE, false },
	{ "insert", VK_INSERT, true },
	{ "delete", VK_DELETE, true },
	{ "home", VK_HOME, true },
	{ "end", VK_END, true },
	{ "pageup", VK_PRIOR, true },
	{ "pagedown", VK_NEXT, true },
	{ "up", VK_UP, true },
	{ "down", VK_DOWN, true },
	{ "left", VK_LEFT, true },
	{ "right", VK_RIGHT, true },
	{ "capslock", VK_CAPITAL, false },
	{ "numlock", VK_NUMLOCK, true },
	{ "scrolllock", VK_SCROLL, false },
	{ "printscreen", VK_SNAPSHOT, true },
	{ "pause", VK_PAUSE, false },
	{ "apps", VK_APPS, true },
	{ "numpad0", VK_NUMPAD0, false },
	{ "numpad1", VK_NUMPAD1, false },
	{ "numpad2", VK_NUMPAD2, false },
	{ "numpad3", VK_NUMPAD3, false },
	{ "numpad4", VK_NUMPAD4, false },
	{ "numpad5", VK_NUMPAD5, false },
	{ "numpad6", VK_NUMPAD6, false },
	{ "numpad7", VK_NUMPAD7, false },
	{ "numpad8", VK_NUMPAD8, false },
	{ "numpad9", VK_NUMPAD9, false },
	{ "numpadadd", VK_ADD, false },
	{ "numpadsubtract", VK_SUBTRACT, false },
	{ "numpadmultiply", VK_MULTIPLY, false },
	{ "numpaddivide", VK_DIVIDE, true },
	{ "numpaddecimal", VK_DECIMAL, false },
	{ "numpadenter", VK_RETURN, true },
	{ "volumeup", VK_VOLUME_UP, true },
	{ "volumedown", VK_VOLUME_DOWN, true },
	{ "volumemute", VK_VOLUME_MUTE, true },
	{ "medianext", VK_MEDIA_NEXT_TRACK, true },
	{ "mediaprev", VK_MEDIA_PREV_TRACK, true },
	{ "mediaplaypause", VK_MEDIA_PLAY_PAUSE, true },
	{ "mediastop", VK_MEDIA_STOP, true },
};

static bool LookupNamed(const string& name, KeyCode& code) {
	// The fixed keys, plus f1 to f24
	for (auto& key : _namedKeys) {
		if (name == key.Name) {
			code.VK = key.VK;
			code.Extended = key.Extended;
			return true;
		}
	}
	if (name.size() >= 2 && name.size() <= 3 && name[0] == 'f') {
		int n = atoi(name.c_str() + 1);
		if (n >= 1 && n <= 24 && to_string(n) == name.substr(1)) {
			code.VK = VK_F1 + n - 1;
			return true;
		}
	}
	return false;
}

HKL ForegroundLayout() {
	// The keyboard layout of whatever has the focus, since that's who gets the keys
	HWND hwnd = GetForegroundWindow();
	return GetKeyboardLayout(hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0);
}

bool LookupKey(const string& name, HKL layout, KeyCode& code) {
	// Named keys first, then a single character through the layout
	code = KeyCode();
	if (LookupNamed(name, code)) {
		return true;
	}
	wstring wide = Utf8ToUtf16(name);
	if (wide.size() != 1) {
		return false;
	}
	SHORT scan = VkKeyScanExW(wide[0], layout);
	if (scan == -1) {
		return false;
	}
	code.VK = LOBYTE(scan);
	int shift = HIBYTE(scan);
	if (shift & 1) {
		code.ImpliedMods |= MOD_KEY_SHIFT;
	}
	if (shift & 2) {
		code.ImpliedMods |= MOD_KEY_CTRL;
	}
	if (shift & 4) {
		code.ImpliedMods |= MOD_KEY_ALT;
	}
	return true;
}

bool IsKnownKeyName(const string& name) {
	KeyCode code;
	return LookupNamed(name, code);
}

UINT ModsToHotkeyFlags(int mods) {
	// Our modifier bits to RegisterHotKey's
	UINT ret = 0;
	if (mods & MOD_KEY_CTRL) {
		ret |= MOD_CONTROL;
	}
	if (mods & MOD_KEY_ALT) {
		ret |= MOD_ALT;
	}
	if (mods & MOD_KEY_SHIFT) {
		ret |= MOD_SHIFT;
	}
	if (mods & MOD_KEY_META) {
		ret |= MOD_WIN;
	}
	return ret;
}
