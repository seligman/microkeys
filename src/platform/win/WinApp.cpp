#include "WinApp.h"
#include "WinKeys.h"
#include "WinText.h"
#include "WinUpdate.h"
#include "Resource.h"
#include "../../core/Text.h"
#include "../../core/UpdateCore.h"
#include <dwmapi.h>
#include <shlwapi.h>

#define LOG_FILE_MAX		(1024 * 1024)
#define LOG_WINDOW_MAX		(512 * 1024)

WinApp* _app = nullptr;
HANDLE _instanceMutex = NULL;

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (_app != nullptr) {
		return _app->WndProc(hWnd, message, wParam, lParam);
	}
	return DefWindowProcW(hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
	// Runs on the UI thread for every key anyone presses, so it stays quick
	if (code == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && _app != nullptr) {
		const KBDLLHOOKSTRUCT* kb = (const KBDLLHOOKSTRUCT*)lParam;
		if (kb->dwExtraInfo != MICROKEYS_INPUT_TAG) {
			_app->OnKeyDown(kb);
		}
	}
	return CallNextHookEx(NULL, code, wParam, lParam);
}

static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
	// A click can move the caret, so what was typed before no longer counts
	if (code == HC_ACTION && _app != nullptr) {
		if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN) {
			_app->OnMouseButton();
		}
	}
	return CallNextHookEx(NULL, code, wParam, lParam);
}

static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD thread, DWORD time) {
	if (_app != nullptr) {
		_app->OnWinEvent(event, hwnd, idObject, idChild);
	}
}

string KnownFolder(REFKNOWNFOLDERID id) {
	// A shell folder like Documents, or "" if it can't be found
	PWSTR path = NULL;
	string ret;
	if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, NULL, &path))) {
		ret = Utf16ToUtf8(path);
	}
	CoTaskMemFree(path);
	return ret;
}

string ExeDir() {
	// The folder this exe is in
	wchar_t path[MAX_PATH * 2];
	DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH * 2);
	wstring full(path, len);
	return Utf16ToUtf8(full.substr(0, full.find_last_of(L'\\')));
}

static string Timestamp() {
	SYSTEMTIME st;
	GetLocalTime(&st);
	char buffer[32];
	sprintf_s(buffer, "%02d:%02d:%02d ", st.wHour, st.wMinute, st.wSecond);
	return buffer;
}

WinApp::WinApp() {
	_app = this;
}

WinApp::~WinApp() {
	if (_logFile != INVALID_HANDLE_VALUE) {
		CloseHandle(_logFile);
	}
	_app = nullptr;
}

bool WinApp::Init(HINSTANCE hInstance, const AppOptions& options) {
	// Set up folders, the window, the tray icon, and start the scripts
	_hInst = hInstance;
	_options = options;
	_appDir = ExeDir();
	string parent = _appDir.substr(0, _appDir.find_last_of('\\'));
	string folder = _appDir.substr(_appDir.find_last_of('\\') + 1);
	_installDir = folder.rfind("app-", 0) == 0 ? parent : _appDir;

	_dataDir = options.DataDir.empty() ? KnownFolder(FOLDERID_RoamingAppData) + "\\MicroKeys" : options.DataDir;
	_localDir = options.DataDir.empty() ? KnownFolder(FOLDERID_LocalAppData) + "\\MicroKeys" : options.DataDir;
	_docsDir = KnownFolder(FOLDERID_Documents);
	CreateDirectoryW(Utf8ToUtf16(_dataDir).c_str(), NULL);
	CreateDirectoryW(Utf8ToUtf16(_localDir).c_str(), NULL);
	_logPath = options.LogFile.empty() ? _localDir + "\\MicroKeys.log" : options.LogFile;
	wstring logFolder = Utf8ToUtf16(_logPath.substr(0, _logPath.find_last_of('\\')));
	SHCreateDirectoryExW(NULL, logFolder.c_str(), NULL);

	if (_options.Headless && AttachConsole(ATTACH_PARENT_PROCESS)) {
		_console = GetStdHandle(STD_OUTPUT_HANDLE);
	}

	_icon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_MICROKEYS), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
	_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
	_triggers = make_unique<Triggers>(this);
	_updater = make_unique<Updater>(this);

	CreateMainWindow();
	if (_hWnd == NULL) {
		return false;
	}
	_platform.ClipboardOwner = _hWnd;
	_platform.OnToast = [this](const string& title, const string& text) { ShowToast(title, text); };

	_foreground = DescribeWindow(GetForegroundWindow());
	_foregroundLayout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), NULL));
	if (!_options.Headless) {
		AddTrayIcon();
		_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
		_busyIcon = MakeBusyIcon();
	}

	StartHost();
	if (!_options.Headless) {
		_updater->Startup(_options.UpdatedFrom, _options.UpdatedVersion);
	}
	return true;
}

void WinApp::StartHost() {
	// Python and the scripts load on the script thread; anything asked for on
	// the command line runs once they have
	HostStartup startup;
	startup.AppDir = _appDir;
	startup.LibDir = _appDir + "\\lib";
	startup.DataDir = _dataDir;
	startup.DocumentsDir = _docsDir;
	startup.ScriptDir = _options.ScriptDir;
	startup.PythonZip = _appDir + "\\" MK_PYLIB ".zip";
	startup.Headless = _options.Headless;
	_host = make_unique<ScriptHost>(this, &_platform);
	_host->Start(startup);
	for (auto& cmd : _options.Commands) {
		_host->PostCommand(cmd);
	}
	if (_options.Headless) {
		HWND hwnd = _hWnd;
		ScriptHost* host = _host.get();
		thread([hwnd, host] {
			host->WaitIdle(-1);
			PostMessageW(hwnd, WM_APP_HEADLESS_DONE, 0, 0);
		}).detach();
	}
}

void WinApp::CreateMainWindow() {
	// The log window.  It doubles as the window that owns the tray icon,
	// the hotkeys, and the clipboard, and stays hidden until asked for.
	WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = MainWndProc;
	wcex.hInstance = _hInst;
	wcex.hIcon = LoadIconW(_hInst, MAKEINTRESOURCEW(IDI_MICROKEYS));
	wcex.hIconSm = _icon;
	wcex.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
	wcex.lpszClassName = MICROKEYS_WINDOW_CLASS;
	RegisterClassExW(&wcex);

	_hWnd = CreateWindowExW(0, MICROKEYS_WINDOW_CLASS, L"MicroKeys", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, _hInst, NULL);
	if (_hWnd == NULL) {
		return;
	}

	// A sensible size for the current DPI
	UINT dpi = GetDpiForWindow(_hWnd);
	SetWindowPos(_hWnd, NULL, 0, 0, MulDiv(760, dpi, 96), MulDiv(480, dpi, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"",
		WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
		0, 0, 100, 100, _hWnd, NULL, _hInst, NULL);
	SendMessageW(_hEdit, EM_LIMITTEXT, 0x7FFFFFFE, 0);
	ApplyFont();
	Layout();
}

void WinApp::ApplyFont() {
	// A monospaced font sized for the window's monitor
	UINT dpi = GetDpiForWindow(_hWnd);
	HFONT font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
	SendMessageW(_hEdit, WM_SETFONT, (WPARAM)font, TRUE);
	if (_font != NULL) {
		DeleteObject(_font);
	}
	_font = font;
}

void WinApp::Layout() {
	RECT rt;
	GetClientRect(_hWnd, &rt);
	MoveWindow(_hEdit, 0, 0, rt.right - rt.left, rt.bottom - rt.top, TRUE);
}

void WinApp::AddTrayIcon() {
	_nid.cbSize = sizeof(_nid);
	_nid.hWnd = _hWnd;
	_nid.uID = 1;
	_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
	_nid.uCallbackMessage = WM_APP_TRAY;
	_nid.hIcon = _icon;
	wcscpy_s(_nid.szTip, L"MicroKeys");
	_trayAdded = Shell_NotifyIconW(NIM_ADD, &_nid) != FALSE;
	_nid.uVersion = NOTIFYICON_VERSION_4;
	Shell_NotifyIconW(NIM_SETVERSION, &_nid);
	UpdateTray();
}

void WinApp::UpdateTray() {
	// The tooltip and icon tell what MicroKeys is doing
	if (!_trayAdded) {
		return;
	}
	string tip = "MicroKeys";
	if (!_running.empty()) {
		tip += ": running " + _running;
	}
	else if (_triggers->IsPaused()) {
		tip += " (paused)";
	}
	else if (!_lastLoadOk) {
		tip += ": script error";
	}
	wstring wide = Utf8ToUtf16(tip);
	wcsncpy_s(_nid.szTip, wide.c_str(), _TRUNCATE);
	_nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
	Shell_NotifyIconW(NIM_MODIFY, &_nid);
}

void WinApp::ShowToast(const string& title, const string& text) {
	// Any thread.  A tray balloon, which Windows 10 and 11 show as a toast.
	auto payload = new pair<string, string>(title, text);
	if (_hWnd == NULL || !PostMessageW(_hWnd, WM_APP_TOAST, 0, (LPARAM)payload)) {
		delete payload;
	}
}

HICON WinApp::MakeBusyIcon() {
	// The tray icon with an orange dot, shown while a macro runs
	int size = GetSystemMetrics(SM_CXSMICON);
	ICONINFO info = { 0 };
	if (_icon == NULL || !GetIconInfo(_icon, &info)) {
		return NULL;
	}
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = size;
	bmi.bmiHeader.biHeight = -size;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	vector<DWORD> pixels((size_t)size * size);
	vector<DWORD> mask((size_t)size * size);
	HDC hdc = GetDC(NULL);
	bool ok = info.hbmColor != NULL && info.hbmMask != NULL &&
		GetDIBits(hdc, info.hbmColor, 0, size, pixels.data(), &bmi, DIB_RGB_COLORS) == size &&
		GetDIBits(hdc, info.hbmMask, 0, size, mask.data(), &bmi, DIB_RGB_COLORS) == size;
	HICON ret = NULL;
	if (ok) {
		// An icon without an alpha channel says what's see-through in its
		// mask instead (black is solid), so build the alpha from that
		bool hasAlpha = false;
		for (DWORD px : pixels) {
			if (px & 0xFF000000) {
				hasAlpha = true;
				break;
			}
		}
		if (!hasAlpha) {
			for (size_t i = 0; i < pixels.size(); i++) {
				pixels[i] = (mask[i] & 0x00FFFFFF) == 0 ? (pixels[i] | 0xFF000000) : 0;
			}
		}
		int radius = size / 4 + 1;
		int cx = size - radius, cy = size - radius;
		for (int y = 0; y < size; y++) {
			for (int x = 0; x < size; x++) {
				int dx = x - cx, dy = y - cy;
				if (dx * dx + dy * dy <= radius * radius) {
					pixels[(size_t)y * size + x] = 0xFFFF8C00;
				}
			}
		}
		void* bits = NULL;
		HBITMAP color = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
		vector<BYTE> zeros((size_t)((size + 15) / 16) * 2 * size);
		HBITMAP solid = CreateBitmap(size, size, 1, 1, zeros.data());
		if (color != NULL && solid != NULL) {
			memcpy(bits, pixels.data(), pixels.size() * sizeof(DWORD));
			ICONINFO busy = { TRUE, 0, 0, solid, color };
			ret = CreateIconIndirect(&busy);
		}
		if (color != NULL) {
			DeleteObject(color);
		}
		if (solid != NULL) {
			DeleteObject(solid);
		}
	}
	ReleaseDC(NULL, hdc);
	if (info.hbmColor != NULL) {
		DeleteObject(info.hbmColor);
	}
	if (info.hbmMask != NULL) {
		DeleteObject(info.hbmMask);
	}
	return ret;
}

int WinApp::Run() {
	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	return (int)msg.wParam;
}

void WinApp::WriteLogFile(const string& text) {
	// Called with _logLock held.  Keeps the log under LOG_FILE_MAX by moving
	// a full one aside.
	if (_logFile == INVALID_HANDLE_VALUE) {
		_logFile = CreateFileW(Utf8ToUtf16(_logPath).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (_logFile == INVALID_HANDLE_VALUE) {
			return;
		}
	}
	LARGE_INTEGER size;
	if (_options.LogFile.empty() && GetFileSizeEx(_logFile, &size) && size.QuadPart > LOG_FILE_MAX) {
		CloseHandle(_logFile);
		wstring path = Utf8ToUtf16(_logPath);
		MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
		_logFile = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (_logFile == INVALID_HANDLE_VALUE) {
			return;
		}
	}
	DWORD written;
	WriteFile(_logFile, text.c_str(), (DWORD)text.size(), &written, NULL);
}

void WinApp::Log(int level, const string& text) {
	// Any thread.  Every line goes to the log file right away, and to the log
	// window once the UI thread gets to it.
	string prefix = Timestamp();
	if (level == LOG_ERROR) {
		prefix += "! ";
	}
	string line;
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		string part = text.substr(start, end == string::npos ? string::npos : end - start);
		if (!part.empty() && part.back() == '\r') {
			part.pop_back();
		}
		line += (start == 0 ? prefix : string(prefix.size(), ' ')) + part + "\r\n";
		if (end == string::npos) {
			break;
		}
		start = end + 1;
	}
	{
		lock_guard<mutex> guard(_logLock);
		WriteLogFile(line);
		_logQueue.push_back(line);
		while (_logQueue.size() > 5000) {
			_logQueue.pop_front();
		}
		if (_console != NULL && _console != INVALID_HANDLE_VALUE) {
			DWORD written;
			WriteFile(_console, line.c_str(), (DWORD)line.size(), &written, NULL);
		}
	}
	if (_hWnd != NULL) {
		PostMessageW(_hWnd, WM_APP_LOG, 0, 0);
	}
}

void WinApp::AppendLogText() {
	// Move queued log lines into the log window
	string text;
	{
		lock_guard<mutex> guard(_logLock);
		while (!_logQueue.empty()) {
			text += _logQueue.front();
			_logQueue.pop_front();
		}
	}
	if (text.empty() || _hEdit == NULL) {
		return;
	}
	int len = GetWindowTextLengthW(_hEdit);
	if (len > LOG_WINDOW_MAX) {
		// Drop the older half, on a line boundary
		SendMessageW(_hEdit, EM_SETSEL, 0, SendMessageW(_hEdit, EM_LINEINDEX, SendMessageW(_hEdit, EM_LINEFROMCHAR, len / 2, 0), 0));
		SendMessageW(_hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"");
		len = GetWindowTextLengthW(_hEdit);
	}
	SendMessageW(_hEdit, EM_SETSEL, len, len);
	SendMessageW(_hEdit, EM_REPLACESEL, FALSE, (LPARAM)Utf8ToUtf16(text).c_str());
}

void WinApp::SetBindings(const Bindings& bindings) {
	auto payload = new Bindings(bindings);
	if (_hWnd == NULL || !PostMessageW(_hWnd, WM_APP_BINDINGS, 0, (LPARAM)payload)) {
		delete payload;
	}
}

void WinApp::Configure(const ShellConfig& config) {
	auto payload = new ShellConfig(config);
	if (_hWnd == NULL || !PostMessageW(_hWnd, WM_APP_CONFIGURE, 0, (LPARAM)payload)) {
		delete payload;
	}
}

void WinApp::ScriptsLoaded(bool ok, const string& summary) {
	auto payload = new pair<bool, string>(ok, summary);
	if (_hWnd == NULL || !PostMessageW(_hWnd, WM_APP_LOADED, 0, (LPARAM)payload)) {
		delete payload;
	}
}

void WinApp::MacroState(const string& running) {
	auto payload = new string(running);
	if (_hWnd == NULL || !PostMessageW(_hWnd, WM_APP_MACRO, 0, (LPARAM)payload)) {
		delete payload;
	}
}

bool WinApp::RegisterHotkey(int id, int mods, const string& key, string& error) {
	// No hotkeys in headless mode, so tests can run next to a real copy
	if (_options.Headless) {
		return true;
	}
	KeyCode code;
	if (!LookupKey(key, GetKeyboardLayout(0), code)) {
		error = "the key isn't on this keyboard layout";
		return false;
	}
	if (!RegisterHotKey(_hWnd, id, ModsToHotkeyFlags(mods | code.ImpliedMods) | MOD_NOREPEAT, code.VK)) {
		DWORD err = GetLastError();
		error = err == ERROR_HOTKEY_ALREADY_REGISTERED ? "another program is already using it" : LastErrorText(err);
		return false;
	}
	return true;
}

void WinApp::UnregisterHotkey(int id) {
	if (!_options.Headless) {
		UnregisterHotKey(_hWnd, id);
	}
}

void WinApp::Fire(int kind, int id, const string& extra) {
	if (kind == FIRE_HOTKEY) {
		Log(LOG_DEBUG, _triggers->HotkeyDescription(id) + " pressed");
	}
	_host->PostFire(kind, id, extra);
}

void WinApp::TriggerLog(int level, const string& text) {
	Log(level, text);
}

void WinApp::ApplyConfig(const ShellConfig& config) {
	// New settings from the scripts' side: the folder to watch, the abort
	// hotkey, and the update feed
	if (_options.Headless) {
		_config = config;
		return;
	}
	if (!_configured || config.ScriptDir != _config.ScriptDir) {
		StartWatcher(config.ScriptDir);
	}
	if (!_configured || config.AbortMods != _config.AbortMods || config.AbortKey != _config.AbortKey) {
		UnregisterHotKey(_hWnd, ABORT_HOTKEY_ID);
		KeyCode code;
		if (!LookupKey(config.AbortKey, GetKeyboardLayout(0), code) ||
			!RegisterHotKey(_hWnd, ABORT_HOTKEY_ID, ModsToHotkeyFlags(config.AbortMods | code.ImpliedMods) | MOD_NOREPEAT, code.VK)) {
			Log(LOG_ERROR, "Unable to register the abort hotkey: " + LastErrorText());
		}
	}
	if (!_configured || config.UpdateFeed != _config.UpdateFeed || config.CheckUpdates != _config.CheckUpdates) {
		_updater->Configure(config.UpdateFeed, config.CheckUpdates);
	}
	_config = config;
	_configured = true;
}

void WinApp::UpdateHooks() {
	// The keyboard and mouse hooks only run while there are hotstrings, and
	// title changes are only watched when a binding cares about titles
	bool wantKeys = _triggers->HasHotstrings() && !_options.Headless && !_suspended;
	if (wantKeys && _keyboardHook == NULL) {
		_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, _hInst, 0);
		_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, _hInst, 0);
	}
	else if (!wantKeys && _keyboardHook != NULL) {
		UnhookWindowsHookEx(_keyboardHook);
		_keyboardHook = NULL;
		if (_mouseHook != NULL) {
			UnhookWindowsHookEx(_mouseHook);
			_mouseHook = NULL;
		}
	}
	// Foreground events are the main signal; a slow poll covers any that go
	// missing, so a per-app binding is never stuck on the wrong window
	if (_triggers->HasFilters() && !_options.Headless && !_suspended) {
		SetTimer(_hWnd, TIMER_FOREGROUND, 500, NULL);
	}
	else {
		KillTimer(_hWnd, TIMER_FOREGROUND);
	}

	bool wantTitles = _triggers->WatchesTitles() && !_options.Headless && !_suspended;
	if (wantTitles && _titleHook == NULL) {
		_titleHook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL, WinEventProc, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
	}
	else if (!wantTitles && _titleHook != NULL) {
		UnhookWinEvent(_titleHook);
		_titleHook = NULL;
	}
}

void WinApp::OnWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild) {
	if (event == EVENT_SYSTEM_FOREGROUND) {
		RefreshForeground(true);
	}
	else if (event == EVENT_OBJECT_NAMECHANGE && idObject == OBJID_WINDOW && idChild == CHILDID_SELF && hwnd == GetForegroundWindow()) {
		RefreshForeground(false);
	}
}

void WinApp::RefreshForeground(bool newWindow) {
	// Keep a copy of the foreground window's details, since the keyboard hook
	// can't afford to look them up on every key
	HWND hwnd = GetForegroundWindow();
	_foreground = DescribeWindow(hwnd);
	_foregroundLayout = GetKeyboardLayout(GetWindowThreadProcessId(hwnd, NULL));
	if (newWindow) {
		_triggers->OnForeground(_foreground);
	}
	else {
		_triggers->OnTitleChange(_foreground);
	}
}

void WinApp::OnMouseButton() {
	_triggers->ResetBuffer();
}

void WinApp::OnKeyDown(const KBDLLHOOKSTRUCT* kb) {
	// Turn a key press into the character it types, for hotstrings
	UINT vk = kb->vkCode;
	switch (vk) {
	case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
	case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
	case VK_MENU: case VK_LMENU: case VK_RMENU:
	case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
		return;
	case VK_BACK:
		_triggers->OnBackspace();
		return;
	case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_HOME: case VK_END:
	case VK_PRIOR: case VK_NEXT: case VK_DELETE: case VK_INSERT: case VK_ESCAPE:
	case VK_LWIN: case VK_RWIN:
		_triggers->ResetBuffer();
		return;
	}

	bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
	bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
	bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
	if (win || (ctrl != alt)) {
		// A shortcut, not typing (Ctrl and Alt together is AltGr)
		_triggers->ResetBuffer();
		return;
	}
	if (vk == VK_PACKET) {
		// Text sent as characters (the on-screen keyboard, other tools)
		_triggers->OnChar((char32_t)kb->scanCode, _foreground);
		return;
	}
	if (vk == VK_RETURN) {
		_triggers->OnChar('\n', _foreground);
		return;
	}
	if (vk == VK_TAB) {
		_triggers->OnChar('\t', _foreground);
		return;
	}

	BYTE state[256] = { 0 };
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
		state[VK_SHIFT] = 0x80;
	}
	if (ctrl && alt) {
		state[VK_CONTROL] = 0x80;
		state[VK_MENU] = 0x80;
	}
	if (GetKeyState(VK_CAPITAL) & 1) {
		state[VK_CAPITAL] = 0x01;
	}
	wchar_t chars[4];
	// Flag 4: don't disturb the keyboard state, so dead keys still work for the user
	int n = ToUnicodeEx(vk, kb->scanCode, state, chars, 4, 4, _foregroundLayout);
	if (n == 1 && chars[0] >= 0x20) {
		_triggers->OnChar(chars[0], _foreground);
	}
	else if (n == 2 && IS_HIGH_SURROGATE(chars[0]) && IS_LOW_SURROGATE(chars[1])) {
		_triggers->OnChar(0x10000 + ((chars[0] - 0xD800) << 10) + (chars[1] - 0xDC00), _foreground);
	}
}

bool WinApp::PassAltGr(UINT mods, UINT vk) {
	// To Windows, AltGr is Ctrl+Alt, so a Ctrl+Alt hotkey also catches AltGr
	// characters (AltGr+E is the euro sign on a German layout).  When the
	// right Alt is down and the layout has a character there, type that
	// character instead of running the macro.
	if ((mods & (MOD_CONTROL | MOD_ALT)) != (MOD_CONTROL | MOD_ALT) || (mods & MOD_WIN) != 0 ||
		(GetAsyncKeyState(VK_RMENU) & 0x8000) == 0) {
		return false;
	}
	HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), NULL));
	BYTE state[256] = { 0 };
	state[VK_CONTROL] = 0x80;
	state[VK_MENU] = 0x80;
	if (mods & MOD_SHIFT) {
		state[VK_SHIFT] = 0x80;
	}
	wchar_t chars[4];
	int n = ToUnicodeEx(vk, MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout), state, chars, 4, 4, layout);
	if (n <= 0 || chars[0] < 0x20) {
		return false;
	}
	vector<KeyAction> actions;
	for (char32_t c : Utf8ToU32(Utf16ToUtf8(chars, n))) {
		KeyAction action;
		action.Character = c;
		actions.push_back(action);
		_triggers->OnChar(c, _foreground);
	}
	atomic<bool> never(false);
	string error;
	_platform.SendKeys(actions, 0, never, error);
	return true;
}

void WinApp::Abort() {
	// The abort hotkey: stop the running macro and anything queued behind it
	if (_host->IsBusy()) {
		Log(LOG_INFO, "Abort hotkey pressed, stopping the running macro");
		_host->Cancel();
	}
	else {
		Log(LOG_INFO, "Abort hotkey pressed, but nothing is running");
	}
}

void WinApp::StartWatcher(const string& dir) {
	// Watch the script folder; any .py change triggers a reload
	StopWatcher();
	_watchDir = dir;
	_watchStop = CreateEventW(NULL, TRUE, FALSE, NULL);
	_watcher = thread(&WinApp::WatchThread, this, Utf8ToUtf16(dir), _watchStop);
}

void WinApp::StopWatcher() {
	if (_watchStop != NULL) {
		SetEvent(_watchStop);
		if (_watcher.joinable()) {
			_watcher.join();
		}
		CloseHandle(_watchStop);
		_watchStop = NULL;
	}
}

void WinApp::WatchThread(wstring dir, HANDLE stop) {
	// Overlapped, so StopWatcher() can end it.  Only reads the directory; never
	// holds a script open, so editors can save however they like.
	HANDLE hDir = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
	if (hDir == INVALID_HANDLE_VALUE) {
		Log(LOG_ERROR, "Unable to watch " + Utf16ToUtf8(dir) + " for changes: " + LastErrorText());
		return;
	}
	OVERLAPPED ov = { 0 };
	ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	vector<DWORD> buffer(16384);
	while (true) {
		ResetEvent(ov.hEvent);
		if (!ReadDirectoryChangesW(hDir, buffer.data(), (DWORD)(buffer.size() * sizeof(DWORD)), FALSE,
			FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE, NULL, &ov, NULL)) {
			break;
		}
		HANDLE handles[2] = { ov.hEvent, stop };
		DWORD bytes = 0;
		if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) != WAIT_OBJECT_0) {
			CancelIoEx(hDir, &ov);
			GetOverlappedResult(hDir, &ov, &bytes, TRUE);
			break;
		}
		if (!GetOverlappedResult(hDir, &ov, &bytes, FALSE)) {
			break;
		}
		// Zero bytes means the buffer overflowed, so assume something relevant changed
		bool relevant = bytes == 0;
		BYTE* cur = (BYTE*)buffer.data();
		while (bytes > 0) {
			FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)cur;
			wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
			if (name.size() > 3 && _wcsicmp(name.c_str() + name.size() - 3, L".py") == 0) {
				relevant = true;
			}
			if (info->NextEntryOffset == 0) {
				break;
			}
			cur += info->NextEntryOffset;
		}
		if (relevant) {
			PostMessageW(_hWnd, WM_APP_SCRIPTS_CHANGED, 0, 0);
		}
	}
	CloseHandle(ov.hEvent);
	CloseHandle(hDir);
}

bool WinApp::RunAtLogin() {
	wchar_t value[1024];
	DWORD size = sizeof(value);
	return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"MicroKeys",
		RRF_RT_REG_SZ, NULL, value, &size) == ERROR_SUCCESS;
}

void WinApp::SetRunAtLogin(bool enable) {
	// Start through the launcher when there is one, so updates are picked up
	HKEY key;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
		return;
	}
	if (enable) {
		wstring exe = Utf8ToUtf16(_installDir + "\\MicroKeys.exe");
		if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
			exe = Utf8ToUtf16(_appDir + "\\MicroKeysApp.exe");
		}
		wstring cmd = L"\"" + exe + L"\"";
		RegSetValueExW(key, L"MicroKeys", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
	}
	else {
		RegDeleteValueW(key, L"MicroKeys");
	}
	RegCloseKey(key);
}

void WinApp::ShowLog() {
	ShowWindow(_hWnd, IsIconic(_hWnd) ? SW_RESTORE : SW_SHOW);
	SetForegroundWindow(_hWnd);
	AppendLogText();
	int len = GetWindowTextLengthW(_hEdit);
	SendMessageW(_hEdit, EM_SETSEL, len, len);
	SendMessageW(_hEdit, EM_SCROLLCARET, 0, 0);
}

void WinApp::ShowTrayMenu(int x, int y) {
	HMENU menu = CreatePopupMenu();
	AppendMenuW(menu, MF_STRING, ID_SHOWLOG, L"&Show log");
	AppendMenuW(menu, MF_STRING, ID_EDITSCRIPTS, L"&Open script folder");
	AppendMenuW(menu, MF_STRING, ID_FILE_RELOADPYTHON, L"&Reload scripts");
	AppendMenuW(menu, MF_STRING | (_triggers->IsPaused() ? MF_CHECKED : 0), ID_PAUSE, L"&Pause hotkeys");
	AppendMenuW(menu, MF_STRING | (_host->IsBusy() ? 0 : MF_GRAYED), ID_STOPMACRO, L"S&top running macro");
	AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(menu, MF_STRING | (RunAtLogin() ? MF_CHECKED : 0), ID_RUNATLOGIN, L"Start with &Windows");
	AppendMenuW(menu, MF_STRING, ID_OPENSETTINGS, L"Edit se&ttings");
	if (_updater->HasFeed()) {
		AppendMenuW(menu, MF_STRING, ID_CHECKUPDATES, L"Check for &updates");
	}
	AppendMenuW(menu, MF_STRING, ID_DOCS, L"&Documentation");
	AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"&About MicroKeys");
	AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(menu, MF_STRING, IDM_EXIT, L"E&xit");
	SetMenuDefaultItem(menu, ID_SHOWLOG, FALSE);

	// The window has to be in front for the menu to close when clicking away
	SetForegroundWindow(_hWnd);
	TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, x, y, 0, _hWnd, NULL);
	PostMessageW(_hWnd, WM_NULL, 0, 0);
	DestroyMenu(menu);
}

static INT_PTR CALLBACK AboutProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_INITDIALOG:
		SetDlgItemTextW(hDlg, IDC_VERSION, L"Version " _T(VERSION_TEXT) L", Copyright \xA9 " _T(VERSION_YEAR));
		SetDlgItemTextW(hDlg, IDC_PY_VERSION, L"Includes Python " _T(MK_PYVER));
		return TRUE;
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static void OpenPath(const string& path) {
	ShellExecuteW(NULL, L"open", Utf8ToUtf16(path).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

static void OpenInEditor(const string& path) {
	// Often nothing is set up to open .json files; Notepad always works
	wchar_t exe[MAX_PATH];
	DWORD len = MAX_PATH;
	if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L".json", L"open", exe, &len))) {
		OpenPath(path);
	}
	else {
		ShellExecuteW(NULL, L"open", L"notepad.exe", (L"\"" + Utf8ToUtf16(path) + L"\"").c_str(), NULL, SW_SHOWNORMAL);
	}
}

void WinApp::OnCommand(int id) {
	switch (id) {
	case ID_SHOWLOG:
		ShowLog();
		break;
	case ID_EDITSCRIPTS:
		OpenPath(_config.ScriptDir.empty() ? _docsDir : _config.ScriptDir);
		break;
	case ID_FILE_RELOADPYTHON:
		Log(LOG_INFO, "Reloading scripts");
		_host->PostReload();
		break;
	case ID_PAUSE:
		_triggers->SetPaused(!_triggers->IsPaused(), _foreground);
		Log(LOG_INFO, _triggers->IsPaused() ? "Hotkeys and hotstrings paused" : "Hotkeys and hotstrings resumed");
		UpdateTray();
		break;
	case ID_STOPMACRO:
		Abort();
		break;
	case ID_RUNATLOGIN:
		SetRunAtLogin(!RunAtLogin());
		break;
	case ID_OPENSETTINGS:
		OpenInEditor(_dataDir + "\\settings.json");
		break;
	case ID_CHECKUPDATES:
		_updater->Check(true);
		break;
	case ID_DOCS: {
		string docs = _appDir + "\\docs\\index.html";
		OpenPath(GetFileAttributesW(Utf8ToUtf16(docs).c_str()) != INVALID_FILE_ATTRIBUTES ? docs : "https://github.com/seligman/microkeys");
		break;
	}
	case ID_CLEARLOG:
		SetWindowTextW(_hEdit, L"");
		break;
	case IDM_ABOUT:
		DialogBoxW(_hInst, MAKEINTRESOURCEW(IDD_ABOUTBOX), _hWnd, AboutProc);
		break;
	case IDM_EXIT:
		ExitApp();
		break;
	}
}

bool WinApp::Suspend() {
	// Let go of everything another copy of MicroKeys would need: the scripts,
	// the hotkeys, the hooks, the tray icon, the single instance mutex.  Used
	// while an update starts.
	_suspended = true;
	if (_instanceMutex != NULL) {
		ReleaseMutex(_instanceMutex);
		CloseHandle(_instanceMutex);
		_instanceMutex = NULL;
	}
	StopWatcher();
	bool stopped = _host->Stop(5000);
	_triggers->Clear();
	UnregisterHotKey(_hWnd, ABORT_HOTKEY_ID);
	UpdateHooks();
	if (_trayAdded) {
		Shell_NotifyIconW(NIM_DELETE, &_nid);
		_trayAdded = false;
	}
	ShowWindow(_hWnd, SW_HIDE);
	return stopped;
}

void WinApp::Resume() {
	// The update didn't start.  Python can't be started twice in one process,
	// so carry on as before by starting this version again.
	wchar_t exe[MAX_PATH * 2];
	GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
	wstring cmd = L"\"" + wstring(exe) + L"\"";
	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	if (CreateProcessW(exe, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	ExitProcess(0);
}

void WinApp::ExitApp() {
	// Stop the scripts (a stuck macro gets a few seconds), then go
	_exiting = true;
	StopWatcher();
	bool stopped = _host->Stop(3000);
	_triggers->Clear();
	UnregisterHotKey(_hWnd, ABORT_HOTKEY_ID);
	_suspended = true;
	UpdateHooks();
	if (_foregroundHook != NULL) {
		UnhookWinEvent(_foregroundHook);
		_foregroundHook = NULL;
	}
	if (_trayAdded) {
		Shell_NotifyIconW(NIM_DELETE, &_nid);
		_trayAdded = false;
	}
	if (!stopped) {
		Log(LOG_ERROR, "A macro didn't stop in time, ending it");
		ExitProcess(0);
	}
	DestroyWindow(_hWnd);
}

HWND WinApp::Window() {
	return _hWnd;
}

string WinApp::InstallDir() {
	return _installDir;
}

string WinApp::AppDir() {
	return _appDir;
}

string WinApp::DataDir() {
	return _localDir;
}

LRESULT WinApp::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == _taskbarCreated && _taskbarCreated != 0) {
		// Explorer restarted, so the tray icon has to be added again
		if (!_suspended && !_options.Headless) {
			_trayAdded = false;
			AddTrayIcon();
		}
		return 0;
	}

	switch (message) {
	case WM_APP_LOG:
		if (IsWindowVisible(hWnd)) {
			AppendLogText();
		}
		return 0;

	case WM_APP_BINDINGS: {
		unique_ptr<Bindings> bindings((Bindings*)lParam);
		if (!_suspended) {
			_triggers->SetBindings(*bindings, _foreground);
			UpdateHooks();
		}
		return 0;
	}

	case WM_APP_CONFIGURE: {
		unique_ptr<ShellConfig> config((ShellConfig*)lParam);
		if (!_suspended) {
			ApplyConfig(*config);
		}
		return 0;
	}

	case WM_APP_LOADED: {
		unique_ptr<pair<bool, string>> result((pair<bool, string>*)lParam);
		if (!result->first && (_lastLoadOk || result->second != _lastSummary)) {
			ShowToast("MicroKeys script error", result->second + "\nSee the log for details.");
		}
		_lastLoadOk = result->first;
		_lastSummary = result->second;
		UpdateTray();
		return 0;
	}

	case WM_APP_MACRO: {
		unique_ptr<string> running((string*)lParam);
		_running = *running;
		if (_running.empty()) {
			KillTimer(hWnd, TIMER_BUSY_ICON);
			if (_trayAdded) {
				_nid.hIcon = _icon;
			}
		}
		else {
			// Only flag macros that take a moment, so quick ones don't flicker
			SetTimer(hWnd, TIMER_BUSY_ICON, 750, NULL);
		}
		UpdateTray();
		return 0;
	}

	case WM_APP_TOAST: {
		unique_ptr<pair<string, string>> toast((pair<string, string>*)lParam);
		if (_trayAdded) {
			_nid.uFlags = NIF_INFO;
			wcsncpy_s(_nid.szInfoTitle, Utf8ToUtf16(toast->first).c_str(), _TRUNCATE);
			wcsncpy_s(_nid.szInfo, Utf8ToUtf16(toast->second).c_str(), _TRUNCATE);
			_nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
			Shell_NotifyIconW(NIM_MODIFY, &_nid);
		}
		else {
			Log(LOG_INFO, "[" + toast->first + "] " + toast->second);
		}
		return 0;
	}

	case WM_APP_TRAY:
		switch (LOWORD(lParam)) {
		case WM_CONTEXTMENU:
		case WM_RBUTTONUP:
			ShowTrayMenu(GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
			break;
		case NIN_SELECT:
		case NIN_KEYSELECT:
		case WM_LBUTTONDBLCLK:
			ShowLog();
			break;
		}
		return 0;

	case WM_APP_SCRIPTS_CHANGED:
		// Editors often write a file more than once when saving, so settle first
		SetTimer(hWnd, TIMER_RELOAD, 300, NULL);
		return 0;

	case WM_APP_SHOW:
		ShowLog();
		return 0;

	case WM_APP_UPDATE:
		_updater->OnMessage(wParam, lParam);
		return 0;

	case WM_APP_HEADLESS_DONE: {
		_host->Stop(5000);
		int failures = _host->Failures();
		Log(LOG_INFO, "Headless run finished, " + to_string(failures) + " failure(s)");
		PostQuitMessage(failures);
		return 0;
	}

	case WM_TIMER:
		if (wParam == TIMER_RELOAD) {
			KillTimer(hWnd, TIMER_RELOAD);
			Log(LOG_INFO, "Scripts changed, reloading");
			_host->PostReload();
		}
		else if (wParam == TIMER_BUSY_ICON) {
			KillTimer(hWnd, TIMER_BUSY_ICON);
			if (!_running.empty() && _trayAdded && _busyIcon != NULL) {
				_nid.uFlags = NIF_ICON;
				_nid.hIcon = _busyIcon;
				Shell_NotifyIconW(NIM_MODIFY, &_nid);
			}
		}
		else if (wParam == TIMER_FOREGROUND) {
			if ((uint64_t)(uintptr_t)GetForegroundWindow() != _foreground.Handle) {
				RefreshForeground(true);
			}
		}
		else if (wParam == TIMER_UPDATE_CHECK) {
			// Look hourly whether a day has passed since the last check, so a
			// copy left running for weeks still hears about updates
			SetTimer(hWnd, TIMER_UPDATE_CHECK, 60 * 60 * 1000, NULL);
			_updater->Check(false);
		}
		return 0;

	case WM_HOTKEY:
		if (wParam == ABORT_HOTKEY_ID) {
			Abort();
		}
		else if (!PassAltGr(LOWORD(lParam), HIWORD(lParam))) {
			_triggers->OnHotkey((int)wParam);
		}
		return 0;

	case WM_COPYDATA: {
		// Another copy of MicroKeys passing on "MicroKeys.exe run <name>"
		COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
		if (cds->dwData == CDS_RUN_COMMAND && cds->cbData > 0 && cds->cbData < 4096) {
			_host->PostCommand(string((const char*)cds->lpData, cds->cbData));
			return TRUE;
		}
		return FALSE;
	}

	case WM_COMMAND:
		OnCommand(LOWORD(wParam));
		return 0;

	case WM_SHOWWINDOW:
		// However the window gets shown, it should show what's been logged
		if (wParam) {
			AppendLogText();
		}
		break;

	case WM_SIZE:
		Layout();
		return 0;

	case WM_DPICHANGED: {
		RECT* suggested = (RECT*)lParam;
		SetWindowPos(hWnd, NULL, suggested->left, suggested->top, suggested->right - suggested->left,
			suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
		ApplyFont();
		Layout();
		return 0;
	}

	case WM_CLOSE:
		// Closing the log window just hides it; Exit is in the tray menu
		ShowWindow(hWnd, SW_HIDE);
		return 0;

	case WM_QUERYENDSESSION:
		return TRUE;

	case WM_ENDSESSION:
		if (wParam && !_exiting) {
			ExitApp();
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, message, wParam, lParam);
}
