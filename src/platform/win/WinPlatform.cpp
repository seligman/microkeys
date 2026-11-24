#include "WinPlatform.h"
#include "WinKeys.h"
#include "WinText.h"
#include "Resource.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <tlhelp32.h>
#include <algorithm>

static HWND H(uint64_t handle) {
	return (HWND)(uintptr_t)handle;
}

bool WinPlatform::IsKeyName(const string& name) {
	return IsKnownKeyName(name);
}

static void AddKey(vector<INPUT>& inputs, UINT vk, bool extended, bool up, HKL layout) {
	// One key event, with the scan code so programs that read it are happy
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = (WORD)vk;
	input.ki.wScan = (WORD)MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, layout);
	input.ki.dwFlags = (extended ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
	input.ki.dwExtraInfo = MICROKEYS_INPUT_TAG;
	inputs.push_back(input);
}

static void AddModifiers(vector<INPUT>& inputs, int mods, bool up, HKL layout) {
	// The modifiers a character needs on this layout, like Shift for "+"
	static const pair<int, UINT> order[] = { { MOD_KEY_CTRL, VK_CONTROL }, { MOD_KEY_ALT, VK_MENU }, { MOD_KEY_SHIFT, VK_SHIFT } };
	if (!up) {
		for (auto& cur : order) {
			if (mods & cur.first) {
				AddKey(inputs, cur.second, false, false, layout);
			}
		}
	}
	else {
		for (int i = 2; i >= 0; i--) {
			if (mods & order[i].first) {
				AddKey(inputs, order[i].second, false, true, layout);
			}
		}
	}
}

struct KeyStep {
	// Input events that go out together, then the wait before the next ones
	vector<INPUT> Inputs;
	int WaitMs;
	bool CanStop;
};

static INPUT UnicodeInput(wchar_t unit, bool up) {
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wScan = unit;
	input.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
	input.ki.dwExtraInfo = MICROKEYS_INPUT_TAG;
	return input;
}

static bool CharKey(char32_t c, HKL layout, KeyCode& code) {
	// The key and modifiers that type c on this layout right now, if there are
	// any.  Caps Lock flips Shift for some keys, and a dead key doesn't count.
	if (c >= 0x10000) {
		return false;
	}
	SHORT found = VkKeyScanExW((wchar_t)c, layout);
	if (found == -1) {
		return false;
	}
	BYTE state[256] = { 0 };
	state[VK_CAPITAL] = GetKeyState(VK_CAPITAL) & 1;
	for (int flip : { 0, 1 }) {
		int shift = HIBYTE(found) ^ flip;
		state[VK_SHIFT] = (shift & 1) ? 0x80 : 0;
		state[VK_CONTROL] = (shift & 2) ? 0x80 : 0;
		state[VK_MENU] = (shift & 4) ? 0x80 : 0;
		wchar_t typed[4];
		// Flag 4 leaves the keyboard's dead key state alone
		if (ToUnicodeEx(LOBYTE(found), MapVirtualKeyExW(LOBYTE(found), MAPVK_VK_TO_VSC, layout), state, typed, 4, 4, layout) == 1 && typed[0] == (wchar_t)c) {
			code = KeyCode();
			code.VK = LOBYTE(found);
			code.ImpliedMods = ((shift & 1) ? MOD_KEY_SHIFT : 0) | ((shift & 2) ? MOD_KEY_CTRL : 0) | ((shift & 4) ? MOD_KEY_ALT : 0);
			return true;
		}
	}
	return false;
}

static void AddChar(vector<KeyStep>& steps, char32_t c, HKL layout, int holdMs, int delayMs, bool byKey) {
	// A character typed as text, so it works on any keyboard layout, or with
	// its key when byKey asks and the layout has one.  Its key stays down for
	// holdMs, since some programs (Windows 11's Notepad) lose keys that come
	// straight back up.
	vector<pair<vector<INPUT>, vector<INPUT>>> strokes;
	KeyCode code;
	if (c == '\n' || c == '\t') {
		UINT vk = c == '\n' ? VK_RETURN : VK_TAB;
		strokes.resize(1);
		AddKey(strokes[0].first, vk, false, false, layout);
		AddKey(strokes[0].second, vk, false, true, layout);
	}
	else if (byKey && CharKey(c, layout, code)) {
		strokes.resize(1);
		AddModifiers(strokes[0].first, code.ImpliedMods, false, layout);
		AddKey(strokes[0].first, code.VK, false, false, layout);
		AddKey(strokes[0].second, code.VK, false, true, layout);
		AddModifiers(strokes[0].second, code.ImpliedMods, true, layout);
	}
	else if (c >= 0x10000) {
		c -= 0x10000;
		for (wchar_t unit : { (wchar_t)(0xD800 + (c >> 10)), (wchar_t)(0xDC00 + (c & 0x3FF)) }) {
			strokes.push_back({ { UnicodeInput(unit, false) }, { UnicodeInput(unit, true) } });
		}
	}
	else {
		strokes.push_back({ { UnicodeInput((wchar_t)c, false) }, { UnicodeInput((wchar_t)c, true) } });
	}
	// Never stop halfway through a character, and send the two halves of a
	// surrogate pair back to back
	for (size_t i = 0; i < strokes.size(); i++) {
		steps.push_back({ strokes[i].first, holdMs, i == 0 });
		steps.push_back({ strokes[i].second, i + 1 < strokes.size() ? 0 : delayMs, false });
	}
}

class PreciseWait {
	// Sleep() rounds up to the system timer tick, 15.6 ms unless something
	// asked for finer, so the waits between keys use a high resolution timer
public:
	~PreciseWait() {
		if (_timer != NULL) {
			CloseHandle(_timer);
		}
	}

	void Wait(int ms) {
		if (ms <= 0) {
			return;
		}
		if (_timer == NULL) {
			_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		}
		LARGE_INTEGER due;
		due.QuadPart = -10000LL * ms;
		if (_timer == NULL || !SetWaitableTimer(_timer, &due, 0, NULL, NULL, FALSE) ||
			WaitForSingleObject(_timer, INFINITE) != WAIT_OBJECT_0) {
			Sleep(ms);
		}
	}

private:
	HANDLE _timer = NULL;
};

static void WaitForForeground() {
	// Wait until the program in front is reading its messages again, so keys
	// don't pile up while it's busy (Windows 11's Notepad garbles keys that
	// arrive then).  At most a second, and not for a hung program or for
	// MicroKeys' own windows.
	HWND hwnd = GetForegroundWindow();
	DWORD pid = 0;
	if (hwnd == NULL || GetWindowThreadProcessId(hwnd, &pid) == 0 || pid == GetCurrentProcessId()) {
		return;
	}
	DWORD_PTR result;
	SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &result);
}

bool WinPlatform::UsesDirectInput() {
	// Has the program in front loaded DirectInput?  Mostly games, and the ones
	// that read the keyboard's state once a frame miss keys shorter than that.
	HWND hwnd = GetForegroundWindow();
	DWORD pid = 0;
	if (hwnd == NULL || GetWindowThreadProcessId(hwnd, &pid) == 0) {
		return false;
	}
	HANDLE snap = INVALID_HANDLE_VALUE;
	for (int tries = 0; tries < 3 && snap == INVALID_HANDLE_VALUE; tries++) {
		// Fails with ERROR_BAD_LENGTH if the program is loading a module just then
		snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
		if (snap == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH) {
			return false;
		}
	}
	if (snap == INVALID_HANDLE_VALUE) {
		return false;
	}
	bool found = false;
	MODULEENTRY32W module = { sizeof(module) };
	for (BOOL more = Module32FirstW(snap, &module); more && !found; more = Module32NextW(snap, &module)) {
		found = _wcsicmp(module.szModule, L"dinput8.dll") == 0 || _wcsicmp(module.szModule, L"dinput.dll") == 0;
	}
	CloseHandle(snap);
	return found;
}

static bool Send(const vector<KeyAction>& actions, int delayMs, int holdMs, int frameMs, bool waitForTarget, const atomic<bool>& cancel, string& error) {
	// Turn each action into steps: a key goes down, stays down for holdMs, and
	// delayMs passes after it comes up.  Steps with no wait between them go
	// out together, and a cancel is checked between keys.
	HKL layout = ForegroundLayout();
	bool byKey = false;
	if (frameMs > 0) {
		// A program that uses DirectInput: each key down and each gap lasts a
		// frame, and characters go out as keys, since DirectInput doesn't see
		// text.  Its keys are spaced out enough already, and a game that reads
		// messages once a frame would hold up every key.
		holdMs = max(holdMs, frameMs);
		delayMs = max(delayMs, frameMs);
		byKey = true;
		waitForTarget = false;
	}
	vector<KeyStep> steps;
	for (auto& action : actions) {
		if (action.Type == KeyAction::Char) {
			AddChar(steps, action.Character, layout, holdMs, delayMs, byKey);
			continue;
		}
		KeyCode code;
		if (!LookupKey(action.Key, layout, code)) {
			error = "Unknown key, or not on this keyboard layout: " + action.Key;
			return false;
		}
		bool up = action.Type == KeyAction::Up;
		vector<INPUT> inputs;
		if (!up) {
			AddModifiers(inputs, code.ImpliedMods, false, layout);
		}
		AddKey(inputs, code.VK, code.Extended, up, layout);
		if (up) {
			AddModifiers(inputs, code.ImpliedMods, true, layout);
		}
		steps.push_back({ inputs, up ? delayMs : max(delayMs, holdMs), true });
	}

	PreciseWait timer;
	vector<INPUT> batch;
	bool startsKey = false;
	for (size_t i = 0; i < steps.size(); i++) {
		if (steps[i].CanStop && cancel) {
			return true;
		}
		startsKey = startsKey || steps[i].CanStop;
		batch.insert(batch.end(), steps[i].Inputs.begin(), steps[i].Inputs.end());
		bool last = i + 1 == steps.size();
		if (last || steps[i].WaitMs > 0 || batch.size() >= 64) {
			// Before a key goes down, the program in front catches up
			if (waitForTarget && startsKey) {
				WaitForForeground();
			}
			startsKey = false;
			if (!batch.empty() && SendInput((UINT)batch.size(), batch.data(), sizeof(INPUT)) == 0) {
				error = "Windows didn't accept the keystrokes: " + LastErrorText();
				return false;
			}
			batch.clear();
			if (!last) {
				timer.Wait(steps[i].WaitMs);
			}
		}
	}
	return true;
}

bool WinPlatform::SendKeys(const vector<KeyAction>& actions, int delayMs, int holdMs, int frameMs, const atomic<bool>& cancel, string& error) {
	return Send(actions, delayMs, holdMs, frameMs, true, cancel, error);
}

bool WinPlatform::SendKeysNow(const vector<KeyAction>& actions, string& error) {
	atomic<bool> never(false);
	return Send(actions, 0, 0, 0, false, never, error);
}

bool WinPlatform::ModifiersHeld() {
	// Is the user physically holding Ctrl, Alt, Shift, or Win?
	static const int vks[] = { VK_SHIFT, VK_CONTROL, VK_MENU, VK_LWIN, VK_RWIN };
	for (int vk : vks) {
		if (GetAsyncKeyState(vk) & 0x8000) {
			return true;
		}
	}
	return false;
}

void WinPlatform::GetCursor(int& x, int& y) {
	POINT pt = { 0 };
	GetCursorPos(&pt);
	x = pt.x;
	y = pt.y;
}

void WinPlatform::MoveCursor(int x, int y) {
	SetCursorPos(x, y);
}

bool WinPlatform::MouseButton(const string& button, bool down, string& error) {
	INPUT input = { 0 };
	input.type = INPUT_MOUSE;
	if (button == "left") {
		input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
	}
	else if (button == "right") {
		input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
	}
	else if (button == "middle") {
		input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
	}
	else if (button == "x1" || button == "x2") {
		input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
		input.mi.mouseData = button == "x1" ? XBUTTON1 : XBUTTON2;
	}
	else {
		error = "Unknown mouse button: " + button;
		return false;
	}
	if (SendInput(1, &input, sizeof(INPUT)) == 0) {
		error = "Windows didn't accept the mouse event: " + LastErrorText();
		return false;
	}
	return true;
}

void WinPlatform::MouseWheel(int clicks, bool horizontal) {
	INPUT input = { 0 };
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
	input.mi.mouseData = (DWORD)(clicks * WHEEL_DELTA);
	SendInput(1, &input, sizeof(INPUT));
}

static string ProcessName(DWORD pid) {
	// The exe's file name, like "notepad.exe"
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process == NULL) {
		return "";
	}
	wchar_t path[MAX_PATH * 2];
	DWORD len = MAX_PATH * 2;
	string ret;
	if (QueryFullProcessImageNameW(process, 0, path, &len)) {
		wstring full(path, len);
		size_t slash = full.find_last_of(L"\\/");
		ret = Utf16ToUtf8(slash == wstring::npos ? full : full.substr(slash + 1));
	}
	CloseHandle(process);
	return ret;
}

WindowInfo DescribeWindow(HWND hwnd) {
	// Everything the core and the Python layer want to know about a window
	WindowInfo info;
	if (hwnd == NULL || !IsWindow(hwnd)) {
		return info;
	}
	info.Handle = (uint64_t)(uintptr_t)hwnd;
	int len = GetWindowTextLengthW(hwnd);
	if (len > 0) {
		vector<wchar_t> buffer((size_t)len + 1);
		len = GetWindowTextW(hwnd, buffer.data(), len + 1);
		info.Title = Utf16ToUtf8(buffer.data(), len);
	}
	wchar_t cls[256];
	int clsLen = GetClassNameW(hwnd, cls, 256);
	info.ClassName = Utf16ToUtf8(cls, clsLen > 0 ? clsLen : 0);
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	info.Pid = pid;
	info.Process = ProcessName(pid);
	return info;
}

static bool IsCloaked(HWND hwnd) {
	// Windows on other virtual desktops, or suspended store apps
	DWORD cloaked = 0;
	return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

static BOOL CALLBACK CollectWindows(HWND hwnd, LPARAM lParam) {
	vector<HWND>* list = (vector<HWND>*)lParam;
	list->push_back(hwnd);
	return TRUE;
}

vector<WindowInfo> WinPlatform::ListWindows(bool includeHidden) {
	// Top-level windows, front to back.  By default only the ones a person
	// would find in Alt+Tab: visible, titled, not tool windows, not cloaked.
	vector<HWND> handles;
	EnumWindows(CollectWindows, (LPARAM)&handles);
	vector<WindowInfo> ret;
	for (HWND hwnd : handles) {
		if (!includeHidden) {
			if (!IsWindowVisible(hwnd) || GetWindowTextLengthW(hwnd) == 0 || IsCloaked(hwnd)) {
				continue;
			}
			if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
				continue;
			}
		}
		ret.push_back(DescribeWindow(hwnd));
	}
	return ret;
}

WindowInfo WinPlatform::ForegroundWindow() {
	return DescribeWindow(GetForegroundWindow());
}

bool WinPlatform::GetWindowDetails(uint64_t handle, WindowInfo& info) {
	info = DescribeWindow(H(handle));
	return info.Handle != 0;
}

static bool WaitForeground(HWND hwnd, int ms) {
	for (int waited = 0; waited <= ms; waited += 10) {
		if (GetForegroundWindow() == hwnd) {
			return true;
		}
		Sleep(10);
	}
	return false;
}

bool WinPlatform::ActivateWindow(uint64_t handle) {
	// Windows only lets a program take the foreground in some cases, so try
	// the polite way first, then the usual workarounds
	HWND hwnd = H(handle);
	if (!IsWindow(hwnd)) {
		return false;
	}
	if (IsIconic(hwnd)) {
		ShowWindowAsync(hwnd, SW_RESTORE);
	}
	if (SetForegroundWindow(hwnd) && WaitForeground(hwnd, 100)) {
		return true;
	}

	// A key event from us counts as user input, which unlocks SetForegroundWindow.
	// An Alt key-up on its own doesn't open any menus.
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = VK_MENU;
	input.ki.dwFlags = KEYEVENTF_KEYUP;
	input.ki.dwExtraInfo = MICROKEYS_INPUT_TAG;
	SendInput(1, &input, sizeof(INPUT));
	if (SetForegroundWindow(hwnd) && WaitForeground(hwnd, 100)) {
		return true;
	}

	DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
	DWORD myThread = GetCurrentThreadId();
	bool attached = fgThread != 0 && fgThread != myThread && AttachThreadInput(myThread, fgThread, TRUE);
	BringWindowToTop(hwnd);
	SetForegroundWindow(hwnd);
	if (attached) {
		AttachThreadInput(myThread, fgThread, FALSE);
	}
	return WaitForeground(hwnd, 200);
}

static bool FrameDeltas(HWND hwnd, RECT& window, RECT& frame) {
	// The window rect includes invisible resize borders; the DWM frame doesn't
	if (!::GetWindowRect(hwnd, &window)) {
		return false;
	}
	if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) {
		frame = window;
	}
	return true;
}

bool WinPlatform::GetWindowBounds(uint64_t handle, ScreenRect& rect) {
	// The visible frame, which is what a person would call the window's edges
	RECT window, frame;
	if (!IsWindow(H(handle)) || !FrameDeltas(H(handle), window, frame)) {
		return false;
	}
	rect.X = frame.left;
	rect.Y = frame.top;
	rect.Width = frame.right - frame.left;
	rect.Height = frame.bottom - frame.top;
	return true;
}

bool WinPlatform::SetWindowBounds(uint64_t handle, const ScreenRect& rect) {
	// Place the visible frame at rect, allowing for the invisible borders
	HWND hwnd = H(handle);
	RECT window, frame;
	if (!IsWindow(hwnd) || !FrameDeltas(hwnd, window, frame)) {
		return false;
	}
	int left = frame.left - window.left;
	int top = frame.top - window.top;
	int right = window.right - frame.right;
	int bottom = window.bottom - frame.bottom;
	int x = rect.X - left;
	int y = rect.Y - top;
	if (!SetWindowPos(hwnd, NULL, x, y, rect.Width + left + right, rect.Height + top + bottom,
		SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS)) {
		return false;
	}
	// Async, so a hung program can't hang the macro, but give it a moment so
	// reading the bounds back right after sees the change
	for (int waited = 0; waited < 300; waited += 10) {
		RECT now;
		if (::GetWindowRect(hwnd, &now) && now.left == x && now.top == y) {
			break;
		}
		Sleep(10);
	}
	return true;
}

string WinPlatform::GetWindowState(uint64_t handle) {
	HWND hwnd = H(handle);
	if (IsIconic(hwnd)) {
		return "minimized";
	}
	if (IsZoomed(hwnd)) {
		return "maximized";
	}
	return "normal";
}

bool WinPlatform::SetWindowState(uint64_t handle, const string& state) {
	// Async, so a hung program can't hang the macro; then give it a moment
	HWND hwnd = H(handle);
	int cmd;
	if (state == "minimized") {
		cmd = SW_MINIMIZE;
	}
	else if (state == "maximized") {
		cmd = SW_MAXIMIZE;
	}
	else if (state == "normal") {
		cmd = SW_RESTORE;
	}
	else {
		return false;
	}
	if (!ShowWindowAsync(hwnd, cmd)) {
		return false;
	}
	for (int waited = 0; waited < 500 && GetWindowState(handle) != state; waited += 10) {
		Sleep(10);
	}
	return true;
}

bool WinPlatform::RequestClose(uint64_t handle) {
	return PostMessageW(H(handle), WM_CLOSE, 0, 0) != FALSE;
}

static BOOL CALLBACK CollectMonitors(HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM lParam) {
	vector<HMONITOR>* list = (vector<HMONITOR>*)lParam;
	list->push_back(monitor);
	return TRUE;
}

static vector<HMONITOR> MonitorHandles() {
	// Every monitor, the primary one first
	vector<HMONITOR> ret;
	EnumDisplayMonitors(NULL, NULL, CollectMonitors, (LPARAM)&ret);
	stable_sort(ret.begin(), ret.end(), [](HMONITOR a, HMONITOR b) {
		MONITORINFO ia = { sizeof(ia) }, ib = { sizeof(ib) };
		GetMonitorInfoW(a, &ia);
		GetMonitorInfoW(b, &ib);
		return (ia.dwFlags & MONITORINFOF_PRIMARY) > (ib.dwFlags & MONITORINFOF_PRIMARY);
	});
	return ret;
}

vector<MonitorInfo> WinPlatform::Monitors() {
	vector<MonitorInfo> ret;
	for (HMONITOR monitor : MonitorHandles()) {
		MONITORINFO mi = { sizeof(mi) };
		if (!GetMonitorInfoW(monitor, &mi)) {
			continue;
		}
		MonitorInfo info;
		info.Bounds = { mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top };
		info.Work = { mi.rcWork.left, mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top };
		info.Primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
		UINT dpiX = 96, dpiY = 96;
		if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
			info.Dpi = (int)dpiX;
		}
		ret.push_back(info);
	}
	return ret;
}

int WinPlatform::WindowMonitor(uint64_t handle) {
	HMONITOR monitor = MonitorFromWindow(H(handle), MONITOR_DEFAULTTONEAREST);
	vector<HMONITOR> all = MonitorHandles();
	for (size_t i = 0; i < all.size(); i++) {
		if (all[i] == monitor) {
			return (int)i;
		}
	}
	return 0;
}

string WinPlatform::ShowMessage(const string& text, const string& title, const string& buttons) {
	UINT type = MB_OK;
	if (buttons == "okcancel") {
		type = MB_OKCANCEL;
	}
	else if (buttons == "yesno") {
		type = MB_YESNO;
	}
	else if (buttons == "yesnocancel") {
		type = MB_YESNOCANCEL;
	}
	int ret = MessageBoxW(NULL, Utf8ToUtf16(text).c_str(), Utf8ToUtf16(title).c_str(),
		type | MB_SETFOREGROUND | MB_TOPMOST | MB_ICONINFORMATION);
	switch (ret) {
	case IDOK: return "ok";
	case IDYES: return "yes";
	case IDNO: return "no";
	default: return "cancel";
	}
}

class DialogData {
	// What the input and choice dialogs are given, and what they hand back
public:
	wstring Title;
	wstring Prompt;
	wstring Text;
	vector<wstring> Items;
	int Choice = -1;
};

static void BringToFront(HWND hDlg) {
	// Dialogs from a hotkey should appear in front of what the user was doing
	SetWindowPos(hDlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	SetForegroundWindow(hDlg);
}

static INT_PTR CALLBACK InputProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	DialogData* data = (DialogData*)GetWindowLongPtrW(hDlg, DWLP_USER);
	switch (message) {
	case WM_INITDIALOG:
		data = (DialogData*)lParam;
		SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)data);
		SetWindowTextW(hDlg, data->Title.c_str());
		SetDlgItemTextW(hDlg, IDC_PROMPT, data->Prompt.c_str());
		SetDlgItemTextW(hDlg, IDC_TEXT, data->Text.c_str());
		SendDlgItemMessageW(hDlg, IDC_TEXT, EM_SETSEL, 0, -1);
		BringToFront(hDlg);
		SetFocus(GetDlgItem(hDlg, IDC_TEXT));
		return FALSE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK) {
			int len = GetWindowTextLengthW(GetDlgItem(hDlg, IDC_TEXT));
			vector<wchar_t> buffer((size_t)len + 1);
			GetDlgItemTextW(hDlg, IDC_TEXT, buffer.data(), len + 1);
			data->Text = wstring(buffer.data());
			EndDialog(hDlg, IDOK);
			return TRUE;
		}
		if (LOWORD(wParam) == IDCANCEL) {
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static INT_PTR CALLBACK ChooseProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	DialogData* data = (DialogData*)GetWindowLongPtrW(hDlg, DWLP_USER);
	switch (message) {
	case WM_INITDIALOG:
		data = (DialogData*)lParam;
		SetWindowLongPtrW(hDlg, DWLP_USER, (LONG_PTR)data);
		SetWindowTextW(hDlg, data->Title.c_str());
		SetDlgItemTextW(hDlg, IDC_PROMPT, data->Prompt.c_str());
		for (auto& item : data->Items) {
			SendDlgItemMessageW(hDlg, IDC_LIST, LB_ADDSTRING, 0, (LPARAM)item.c_str());
		}
		SendDlgItemMessageW(hDlg, IDC_LIST, LB_SETCURSEL, 0, 0);
		BringToFront(hDlg);
		SetFocus(GetDlgItem(hDlg, IDC_LIST));
		return FALSE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || (LOWORD(wParam) == IDC_LIST && HIWORD(wParam) == LBN_DBLCLK)) {
			data->Choice = (int)SendDlgItemMessageW(hDlg, IDC_LIST, LB_GETCURSEL, 0, 0);
			EndDialog(hDlg, IDOK);
			return TRUE;
		}
		if (LOWORD(wParam) == IDCANCEL) {
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

bool WinPlatform::AskText(const string& prompt, const string& title, const string& initial, string& result) {
	DialogData data;
	data.Title = Utf8ToUtf16(title);
	data.Prompt = Utf8ToUtf16(prompt);
	data.Text = Utf8ToUtf16(initial);
	if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_INPUT), NULL, InputProc, (LPARAM)&data) != IDOK) {
		return false;
	}
	result = Utf16ToUtf8(data.Text);
	return true;
}

int WinPlatform::AskChoice(const vector<string>& items, const string& prompt, const string& title) {
	DialogData data;
	data.Title = Utf8ToUtf16(title);
	data.Prompt = Utf8ToUtf16(prompt);
	for (auto& item : items) {
		data.Items.push_back(Utf8ToUtf16(item));
	}
	if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_CHOOSE), NULL, ChooseProc, (LPARAM)&data) != IDOK) {
		return -1;
	}
	return data.Choice;
}

void WinPlatform::Toast(const string& title, const string& text) {
	if (OnToast) {
		OnToast(title, text);
	}
}

void WinPlatform::ShowOverlay(const string& text, int timeoutMs) {
	if (OnOverlay) {
		OnOverlay(text, timeoutMs);
	}
}

bool WinPlatform::ShellOpen(const string& target, string& error) {
	// ShellExecute wants COM on the calling thread
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	wstring wide = Utf8ToUtf16(target);
	SHELLEXECUTEINFOW info = { sizeof(info) };
	info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
	info.lpVerb = L"open";
	info.lpFile = wide.c_str();
	info.nShow = SW_SHOWNORMAL;
	bool ok = ShellExecuteExW(&info) != FALSE;
	if (!ok) {
		error = "Unable to open " + target + ": " + LastErrorText();
	}
	if (SUCCEEDED(hr)) {
		CoUninitialize();
	}
	return ok;
}

string WinPlatform::PlatformName() {
	return "windows";
}
