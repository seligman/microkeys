#include "WinPlatform.h"
#include "WinKeys.h"
#include "WinText.h"
#include "Resource.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
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

static void AddChar(vector<INPUT>& inputs, char32_t c, HKL layout) {
	// A character typed as text, so it works on any keyboard layout
	if (c == '\n') {
		AddKey(inputs, VK_RETURN, false, false, layout);
		AddKey(inputs, VK_RETURN, false, true, layout);
		return;
	}
	if (c == '\t') {
		AddKey(inputs, VK_TAB, false, false, layout);
		AddKey(inputs, VK_TAB, false, true, layout);
		return;
	}
	wchar_t units[2];
	int count = 1;
	if (c >= 0x10000) {
		c -= 0x10000;
		units[0] = (wchar_t)(0xD800 + (c >> 10));
		units[1] = (wchar_t)(0xDC00 + (c & 0x3FF));
		count = 2;
	}
	else {
		units[0] = (wchar_t)c;
	}
	for (int i = 0; i < count; i++) {
		for (int up = 0; up < 2; up++) {
			INPUT input = { 0 };
			input.type = INPUT_KEYBOARD;
			input.ki.wScan = units[i];
			input.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
			input.ki.dwExtraInfo = MICROKEYS_INPUT_TAG;
			inputs.push_back(input);
		}
	}
}

bool WinPlatform::SendKeys(const vector<KeyAction>& actions, int delayMs, const atomic<bool>& cancel, string& error) {
	// Turn each action into input events.  With a delay, each action goes out
	// on its own, checking for a cancel between them.
	HKL layout = ForegroundLayout();
	vector<vector<INPUT>> groups;
	for (auto& action : actions) {
		vector<INPUT> inputs;
		if (action.Type == KeyAction::Char) {
			AddChar(inputs, action.Character, layout);
		}
		else {
			KeyCode code;
			if (!LookupKey(action.Key, layout, code)) {
				error = "Unknown key, or not on this keyboard layout: " + action.Key;
				return false;
			}
			bool up = action.Type == KeyAction::Up;
			if (!up) {
				AddModifiers(inputs, code.ImpliedMods, false, layout);
			}
			AddKey(inputs, code.VK, code.Extended, up, layout);
			if (up) {
				AddModifiers(inputs, code.ImpliedMods, true, layout);
			}
		}
		groups.push_back(inputs);
	}

	vector<INPUT> batch;
	for (size_t i = 0; i < groups.size(); i++) {
		if (cancel) {
			return true;
		}
		batch.insert(batch.end(), groups[i].begin(), groups[i].end());
		if (delayMs > 0 || i + 1 == groups.size() || batch.size() >= 64) {
			if (!batch.empty() && SendInput((UINT)batch.size(), batch.data(), sizeof(INPUT)) == 0) {
				error = "Windows didn't accept the keystrokes: " + LastErrorText();
				return false;
			}
			batch.clear();
			if (delayMs > 0 && i + 1 < groups.size()) {
				Sleep(delayMs);
			}
		}
	}
	return true;
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

static bool OpenClipboardRetry(HWND owner) {
	// Another program may have the clipboard open for a moment
	for (int i = 0; i < 20; i++) {
		if (OpenClipboard(owner)) {
			return true;
		}
		Sleep(10);
	}
	return false;
}

bool WinPlatform::GetClipboardText(string& text) {
	// False when the clipboard has no text, or couldn't be opened
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboardRetry(ClipboardOwner)) {
		return false;
	}
	bool ret = false;
	HANDLE data = GetClipboardData(CF_UNICODETEXT);
	if (data != NULL) {
		const wchar_t* locked = (const wchar_t*)GlobalLock(data);
		if (locked != NULL) {
			size_t max = GlobalSize(data) / sizeof(wchar_t);
			size_t len = 0;
			while (len < max && locked[len] != 0) {
				len++;
			}
			text = Utf16ToUtf8(locked, len);
			ret = true;
			GlobalUnlock(data);
		}
	}
	CloseClipboard();
	return ret;
}

bool WinPlatform::SetClipboardText(const string& text) {
	wstring wide = Utf8ToUtf16(text);
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
	if (mem == NULL) {
		return false;
	}
	wchar_t* locked = (wchar_t*)GlobalLock(mem);
	if (locked == NULL) {
		GlobalFree(mem);
		return false;
	}
	memcpy(locked, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
	GlobalUnlock(mem);

	if (!OpenClipboardRetry(ClipboardOwner)) {
		GlobalFree(mem);
		return false;
	}
	EmptyClipboard();
	bool ok = SetClipboardData(CF_UNICODETEXT, mem) != NULL;
	CloseClipboard();
	if (!ok) {
		GlobalFree(mem);
	}
	return ok;
}

uint32_t WinPlatform::ClipboardSequence() {
	return GetClipboardSequenceNumber();
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
