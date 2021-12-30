#include "framework.h"
#include "MicroKeys.h"
#include "py_module_impl.h"

extern "C" void keys_press_invoke(const char* msg) {
	stringstream ss;
	ss << "keys.press('" << msg << "')\r\n";
	LogMessage(ss.str());

	DoEvents();

	for (const char* cur = msg; *cur; cur++) {
		if (cur != msg) {
			Sleep(20);
			DoEvents();
		}

		SHORT vk;
		switch (*cur) {
		case '\n':
			vk = VkKeyScanA(VK_RETURN);
			break;
		default:
			vk = VkKeyScanA(*cur);
			break;
		}
		UINT scanCode = MapVirtualKeyA(vk, 2);
		UINT extended = 0;

		if (scanCode == 0) {
			extended = KEYEVENTF_EXTENDEDKEY;
		}

		scanCode = MapVirtualKeyA(vk, 0);
		bool shift = (vk & 0x100) != 0;
		bool ctrl = (vk & 0x200) != 0;
		bool alt = (vk & 0x400) != 0;
		vk &= 0xff;
		if (shift) { keybd_event(VK_SHIFT, 0, 0, 0); }
		if (ctrl) { keybd_event(VK_CONTROL, 0, 0, 0); }
		if (alt) { keybd_event(VK_MENU, 0, 0, 0); }
		keybd_event((BYTE)vk, scanCode, extended, 0);
		keybd_event((BYTE)vk, scanCode, extended | KEYEVENTF_KEYUP, 0);
		if (alt) { keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0); }
		if (ctrl) { keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); }
		if (shift) { keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0); }
	}
}

void AddDesc(stringstream &desc, string val) {
	if (desc.tellp() > 0) {
		desc << "+";
	}
	desc << val;
}

extern "C" void key_press_store_fun(int vk, char* name, void* fun) {
	KeyData key;
	key.ID = (int)GetKeys().size();
	if (vk != 0) {
		RegisterHotKey(GetMainWindow(), key.ID + 1, (vk & 0xff00) >> 8, vk & 0xff);
	}
	key.VK = vk;
	key.Name = string(name);
	key.PythonFunction = fun;

	stringstream ss;
	if (vk != 0) {
		stringstream desc;
		// Start of generated section -- Do not edit!
		// This section is generated by create_key_constants.py, do not edit by hand
		if ((vk & 0x100) > 0) { AddDesc(desc, "Alt"); }
		if ((vk & 0x200) > 0) { AddDesc(desc, "Control"); }
		if ((vk & 0x400) > 0) { AddDesc(desc, "Shift"); }
		if ((vk & 0x800) > 0) { AddDesc(desc, "Windows"); }
		switch (vk & 0xFF) {
		case 0x08: AddDesc(desc, "Back"); break;
		case 0x09: AddDesc(desc, "Tab"); break;
		case 0x0C: AddDesc(desc, "Clear"); break;
		case 0x0D: AddDesc(desc, "Enter"); break;
		case 0x1B: AddDesc(desc, "Escape"); break;
		case 0x20: AddDesc(desc, "Space"); break;
		case 0x23: AddDesc(desc, "End"); break;
		case 0x24: AddDesc(desc, "Home"); break;
		case 0x25: AddDesc(desc, "Left"); break;
		case 0x26: AddDesc(desc, "Up"); break;
		case 0x27: AddDesc(desc, "Right"); break;
		case 0x28: AddDesc(desc, "Down"); break;
		case 0x2D: AddDesc(desc, "Insert"); break;
		case 0x2E: AddDesc(desc, "Delete"); break;
		case 0x30: AddDesc(desc, "0"); break;
		case 0x31: AddDesc(desc, "1"); break;
		case 0x32: AddDesc(desc, "2"); break;
		case 0x33: AddDesc(desc, "3"); break;
		case 0x34: AddDesc(desc, "4"); break;
		case 0x35: AddDesc(desc, "5"); break;
		case 0x36: AddDesc(desc, "6"); break;
		case 0x37: AddDesc(desc, "7"); break;
		case 0x38: AddDesc(desc, "8"); break;
		case 0x39: AddDesc(desc, "9"); break;
		case 0x41: AddDesc(desc, "A"); break;
		case 0x42: AddDesc(desc, "B"); break;
		case 0x43: AddDesc(desc, "C"); break;
		case 0x44: AddDesc(desc, "D"); break;
		case 0x45: AddDesc(desc, "E"); break;
		case 0x46: AddDesc(desc, "F"); break;
		case 0x47: AddDesc(desc, "G"); break;
		case 0x48: AddDesc(desc, "H"); break;
		case 0x49: AddDesc(desc, "I"); break;
		case 0x4A: AddDesc(desc, "J"); break;
		case 0x4B: AddDesc(desc, "K"); break;
		case 0x4C: AddDesc(desc, "L"); break;
		case 0x4D: AddDesc(desc, "M"); break;
		case 0x4E: AddDesc(desc, "N"); break;
		case 0x4F: AddDesc(desc, "O"); break;
		case 0x50: AddDesc(desc, "P"); break;
		case 0x51: AddDesc(desc, "Q"); break;
		case 0x52: AddDesc(desc, "R"); break;
		case 0x53: AddDesc(desc, "S"); break;
		case 0x54: AddDesc(desc, "T"); break;
		case 0x55: AddDesc(desc, "U"); break;
		case 0x56: AddDesc(desc, "V"); break;
		case 0x57: AddDesc(desc, "W"); break;
		case 0x58: AddDesc(desc, "X"); break;
		case 0x59: AddDesc(desc, "Y"); break;
		case 0x5A: AddDesc(desc, "Z"); break;
		case 0x70: AddDesc(desc, "F1"); break;
		case 0x71: AddDesc(desc, "F2"); break;
		case 0x72: AddDesc(desc, "F3"); break;
		case 0x73: AddDesc(desc, "F4"); break;
		case 0x74: AddDesc(desc, "F5"); break;
		case 0x75: AddDesc(desc, "F6"); break;
		case 0x76: AddDesc(desc, "F7"); break;
		case 0x77: AddDesc(desc, "F8"); break;
		case 0x78: AddDesc(desc, "F9"); break;
		case 0x79: AddDesc(desc, "F10"); break;
		case 0x7A: AddDesc(desc, "F11"); break;
		case 0x7B: AddDesc(desc, "F12"); break;
		default: AddDesc(desc, "<Unknown>"); break;
		}
		// End of generated section -- Do not edit!
		key.Description = desc.str();
		ss << "Registered " << desc.str() << "\r\n";
	}
	else {
		ss << "Registered '" << key.Name << "'\r\n";
	}
	GetKeys().push_back(key);
	LogMessage(ss.str());
}

extern "C" char* clip_copy_invoke() {
	LogMessage("clip.copy()\r\n");

	char* ret = NULL;
	if (OpenClipboard(GetMainWindow())) {
		HANDLE hData = GetClipboardData(CF_TEXT);
		if (hData) {
			char* clip = (char*)GlobalLock(hData);
			if (clip != NULL) {
				ret = (char*)malloc(strlen(clip) + 2);
				if (ret != NULL) {
					strcpy_s(ret, strlen(clip) + 1, clip);
				}
				GlobalUnlock(clip);
			}
		}
		CloseClipboard();
	}
	return ret;
}

extern "C" void clip_paste_invoke(const char* data) {
	stringstream ss;
	ss << "clip.paste('" << data << "')\r\n";
	LogMessage(ss.str());

	if (OpenClipboard(GetMainWindow())) {
		HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, strlen(data) + 2);
		if (hMem != NULL) {
			char* locked = (char*)GlobalLock(hMem);
			if (locked != NULL) {
				strcpy_s(locked, strlen(data) + 2, data);
				GlobalUnlock(hMem);
				EmptyClipboard();
				SetClipboardData(CF_TEXT, hMem);
			}
		}
		CloseClipboard();
	}
}