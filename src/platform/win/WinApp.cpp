#include "WinApp.h"
#include "WinKeys.h"
#include "WinText.h"
#include "WinUpdate.h"
#include "WinOverlay.h"
#include "Resource.h"
#include "../../core/Text.h"
#include "../../core/UpdateCore.h"
#include <dwmapi.h>
#include <shlwapi.h>
#include <richedit.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <regex>

using Microsoft::WRL::ComPtr;

#define LOG_FILE_MAX		(1024 * 1024)
#define LOG_WINDOW_MAX		(512 * 1024)
#define LOG_ERROR_COLOR		RGB(192, 0, 0)
#define LOG_CONSOLE_COLOR	RGB(0, 90, 170)

// What a file name and line look like in the log: a traceback's
// File "C:\...\main.py", line 12, or the main.py:12 of binding messages
static const wregex _tracebackLink(LR"re(File "([^"<][^"]*)", line (\d+))re");
static const wregex _shortLink(LR"re(([\w.\-]+\.py):(\d+))re");

WinApp* _app = nullptr;
HANDLE _instanceMutex = NULL;

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	if (_app != nullptr) {
		return _app->WndProc(hWnd, message, wParam, lParam);
	}
	return DefWindowProcW(hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
	// Runs on the UI thread for every key anyone presses, so it stays quick.
	// Keys a remapped key sends count as typing, but aren't remapped again.
	if (code == HC_ACTION && _app != nullptr) {
		const KBDLLHOOKSTRUCT* kb = (const KBDLLHOOKSTRUCT*)lParam;
		bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
		if (kb->dwExtraInfo != MICROKEYS_INPUT_TAG) {
			if (kb->dwExtraInfo != MICROKEYS_REMAP_TAG && _app->OnRemapKey(kb, down)) {
				return 1;
			}
			if (down) {
				_app->OnKeyDown(kb);
			}
		}
	}
	return CallNextHookEx(NULL, code, wParam, lParam);
}

static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
	// Mouse hotkeys, and a click can move the caret, so what was typed
	// before no longer counts for hotstrings
	if (code == HC_ACTION && _app != nullptr) {
		const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)lParam;
		if (ms->dwExtraInfo != MICROKEYS_INPUT_TAG && _app->OnMouseHotkey((UINT)wParam, ms)) {
			return 1;
		}
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

static LRESULT CALLBACK InputProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
	// The console's input line: Enter, Up, Down and Escape are ours, and Tab
	// indents rather than beeping
	if (message == WM_KEYDOWN && ((WinApp*)data)->OnConsoleKey((UINT)wParam)) {
		return 0;
	}
	if (message == WM_CHAR && (wParam == '\r' || wParam == '\t' || wParam == VK_ESCAPE)) {
		if (wParam == '\t') {
			SendMessageW(hWnd, EM_REPLACESEL, TRUE, (LPARAM)L"    ");
		}
		return 0;
	}
	return DefSubclassProc(hWnd, message, wParam, lParam);
}

static BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM lParam) {
	((set<HWND>*)lParam)->insert(hwnd);
	return TRUE;
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
	_overlay = make_unique<Overlay>(hInstance);

	CreateMainWindow();
	if (_hWnd == NULL) {
		return false;
	}
	_platform.ClipboardOwner = _hWnd;
	_platform.OnToast = [this](const string& title, const string& text) { ShowToast(title, text); };
	_platform.OnOverlay = [this](const string& text, int timeoutMs) { ShowOverlay(text, timeoutMs); };
	if (!_options.Headless) {
		_clipboardListener = AddClipboardFormatListener(_hWnd) != FALSE;
	}

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
	startup.LocalDir = _localDir;
	startup.PythonZip = _appDir + "\\" MK_PYLIB ".zip";
	startup.Headless = _options.Headless;
#ifdef MK_NO_UPDATES
	startup.Updates = false;
#endif
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
	// Running as administrator, Windows keeps messages from ordinary programs
	// out, which includes "MicroKeys.exe run" handing over its commands
	ChangeWindowMessageFilterEx(_hWnd, WM_COPYDATA, MSGFLT_ALLOW, NULL);
	ChangeWindowMessageFilterEx(_hWnd, WM_APP_SHOW, MSGFLT_ALLOW, NULL);

	// A sensible size for the current DPI
	UINT dpi = GetDpiForWindow(_hWnd);
	SetWindowPos(_hWnd, NULL, 0, 0, MulDiv(760, dpi, 96), MulDiv(480, dpi, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	// A rich edit, for coloured errors and file names that can be clicked,
	// with a Python prompt below it
	LoadLibraryW(L"Msftedit.dll");
	_hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
		WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | WS_VSCROLL | WS_HSCROLL,
		0, 0, 100, 100, _hWnd, NULL, _hInst, NULL);
	SendMessageW(_hEdit, EM_EXLIMITTEXT, 0, 0x7FFFFFFE);
	SendMessageW(_hEdit, EM_SETTARGETDEVICE, 0, 1);
	SendMessageW(_hEdit, EM_SETEVENTMASK, 0, ENM_LINK);
	_hPrompt = CreateWindowExW(0, L"Static", L">>>", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX,
		0, 0, 10, 10, _hWnd, NULL, _hInst, NULL);
	_hInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"Edit", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		0, 0, 10, 10, _hWnd, NULL, _hInst, NULL);
	SetWindowSubclass(_hInput, InputProc, 1, (DWORD_PTR)this);
	SendMessageW(_hInput, EM_SETCUEBANNER, TRUE, (LPARAM)L"Python: an expression or statement to run, then Enter");
	ApplyFont();
	Layout();
}

void WinApp::ApplyFont() {
	// A monospaced font sized for the window's monitor
	UINT dpi = GetDpiForWindow(_hWnd);
	HFONT font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
	SendMessageW(_hEdit, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(_hPrompt, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(_hInput, WM_SETFONT, (WPARAM)font, TRUE);
	if (_font != NULL) {
		DeleteObject(_font);
	}
	_font = font;
}

void WinApp::Layout() {
	// The log fills the window above the one-line prompt
	RECT rt;
	GetClientRect(_hWnd, &rt);
	UINT dpi = GetDpiForWindow(_hWnd);
	int width = rt.right - rt.left;
	int height = rt.bottom - rt.top;
	int line = MulDiv(24, dpi, 96);
	int prompt = MulDiv(36, dpi, 96);
	MoveWindow(_hEdit, 0, 0, width, max(height - line, 0), TRUE);
	MoveWindow(_hPrompt, 0, height - line, prompt, line, TRUE);
	MoveWindow(_hInput, prompt, height - line, max(width - prompt, 0), line, TRUE);
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
	string tip = _platform.IsElevated() ? "MicroKeys (administrator)" : "MicroKeys";
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

void WinApp::ShowOverlay(const string& text, int timeoutMs) {
	// Any thread
	auto payload = new pair<string, int>(text, timeoutMs);
	if (_hWnd == NULL || !PostMessageW(_hWnd, WM_APP_OVERLAY, 0, (LPARAM)payload)) {
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
	// Follow new lines only while the last line is in view, so reading or
	// selecting further up isn't disturbed
	GETTEXTLENGTHEX count = { GTL_NUMCHARS | GTL_PRECISE, 1200 };
	LONG len = (LONG)SendMessageW(_hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&count, 0);
	bool following = true;
	if (len > 0) {
		POINTL last = { 0, 0 };
		RECT view;
		SendMessageW(_hEdit, EM_POSFROMCHAR, (WPARAM)&last, len - 1);
		GetClientRect(_hEdit, &view);
		following = last.y < view.bottom;
	}
	POINT scroll = { 0, 0 };
	SendMessageW(_hEdit, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
	CHARRANGE selection;
	SendMessageW(_hEdit, EM_EXGETSEL, 0, (LPARAM)&selection);
	SendMessageW(_hEdit, WM_SETREDRAW, FALSE, 0);
	bool atEnd = selection.cpMin >= len;
	if (len > LOG_WINDOW_MAX) {
		// Drop the older half, on a line boundary
		LONG line = (LONG)SendMessageW(_hEdit, EM_EXLINEFROMCHAR, 0, len / 2);
		CHARRANGE older = { 0, (LONG)SendMessageW(_hEdit, EM_LINEINDEX, line, 0) };
		SendMessageW(_hEdit, EM_EXSETSEL, 0, (LPARAM)&older);
		SendMessageW(_hEdit, EM_REPLACESEL, FALSE, (LPARAM)L"");
		selection.cpMin = max(selection.cpMin - older.cpMax, 0L);
		selection.cpMax = max(selection.cpMax - older.cpMax, 0L);
		following = true;
	}
	CHARRANGE end = { -1, -1 };
	SendMessageW(_hEdit, EM_EXSETSEL, 0, (LPARAM)&end);
	SendMessageW(_hEdit, EM_EXGETSEL, 0, (LPARAM)&end);
	CHARFORMAT2W plain = { sizeof(plain) };
	plain.dwMask = CFM_COLOR | CFM_LINK;
	plain.dwEffects = CFE_AUTOCOLOR;
	SendMessageW(_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&plain);
	wstring wide = Utf8ToUtf16(text);
	SendMessageW(_hEdit, EM_REPLACESEL, FALSE, (LPARAM)wide.c_str());
	FormatLogText(end.cpMin, wide);

	if (atEnd) {
		selection.cpMin = selection.cpMax = -1;
	}
	SendMessageW(_hEdit, EM_EXSETSEL, 0, (LPARAM)&selection);
	if (!following) {
		SendMessageW(_hEdit, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
	}
	SendMessageW(_hEdit, WM_SETREDRAW, TRUE, 0);
	if (following) {
		// EM_SCROLLCARET does nothing while the log doesn't have the focus
		SendMessageW(_hEdit, WM_VSCROLL, SB_BOTTOM, 0);
	}
	InvalidateRect(_hEdit, NULL, TRUE);
}

void WinApp::FormatLogText(LONG start, const wstring& text) {
	// Colour errors and console input, and make file names links.  The rich
	// edit keeps each line break as one character, where the text has two.
	auto apply = [this](LONG from, LONG to, DWORD mask, DWORD effects, COLORREF color) {
		CHARRANGE range = { from, to };
		CHARFORMAT2W cf = { sizeof(cf) };
		cf.dwMask = mask;
		cf.dwEffects = effects;
		cf.crTextColor = color;
		SendMessageW(_hEdit, EM_EXSETSEL, 0, (LPARAM)&range);
		SendMessageW(_hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
	};
	LONG pos = start;
	size_t cur = 0;
	while (cur < text.size()) {
		size_t next = text.find(L"\r\n", cur);
		wstring line = text.substr(cur, next == wstring::npos ? wstring::npos : next - cur);
		// Lines start with "HH:MM:SS ", then "! " for an error; an entry's
		// later lines start with spaces and look like its first
		bool stamped = line.size() > 9 && line[2] == L':' && line[5] == L':';
		if (stamped) {
			_logInError = line.compare(9, 2, L"! ") == 0;
		}
		if (_logInError) {
			apply(pos, pos + (LONG)line.size(), CFM_COLOR, 0, LOG_ERROR_COLOR);
		}
		else if (stamped && (line.compare(9, 4, L">>> ") == 0 || line.compare(9, 4, L"... ") == 0)) {
			apply(pos, pos + (LONG)line.size(), CFM_COLOR, 0, LOG_CONSOLE_COLOR);
		}
		for (auto* pattern : { &_tracebackLink, &_shortLink }) {
			for (wsregex_iterator it(line.begin(), line.end(), *pattern), done; it != done; ++it) {
				LONG from = pos + (LONG)it->position(0);
				apply(from, from + (LONG)it->length(0), CFM_LINK, CFE_LINK, 0);
			}
		}
		pos += (LONG)line.size() + 1;
		if (next == wstring::npos) {
			break;
		}
		cur = next + 2;
	}
}

static void ReplaceAll(wstring& text, const wstring& from, const wstring& to) {
	for (size_t pos = text.find(from); pos != wstring::npos; pos = text.find(from, pos + to.size())) {
		text.replace(pos, from.size(), to);
	}
}

static wstring FirstFile(const vector<wstring>& candidates) {
	for (auto& cur : candidates) {
		if (GetFileAttributesW(cur.c_str()) != INVALID_FILE_ATTRIBUTES) {
			return cur;
		}
	}
	return L"";
}

void WinApp::OpenLink(const wstring& text) {
	// A file name clicked in the log; short names are in the script folder
	wsmatch m;
	if (!regex_search(text, m, _tracebackLink) && !regex_search(text, m, _shortLink)) {
		return;
	}
	string file = Utf16ToUtf8(m[1].str());
	if (PathIsRelativeW(m[1].str().c_str())) {
		file = _config.ScriptDir + "\\" + file;
	}
	OpenAt(file, _wtoi(m[2].str().c_str()));
}

void WinApp::OpenAt(const string& file, int line) {
	// Open a file at a line: with the editor setting if there is one, else
	// VS Code or Notepad++ if one is installed, else Notepad, which can't go
	// to a line
	wstring path = Utf8ToUtf16(file);
	wstring number = to_wstring(line);
	wstring cmd;
	if (!_config.Editor.empty()) {
		cmd = Utf8ToUtf16(_config.Editor);
		if (cmd.find(L"{file}") == wstring::npos) {
			cmd += L" {file}";
		}
		ReplaceAll(cmd, L"\"{file}\"", L"{file}");
		ReplaceAll(cmd, L"{file}", L"\"" + path + L"\"");
		ReplaceAll(cmd, L"{line}", number);
	}
	else {
		wstring local = Utf8ToUtf16(KnownFolder(FOLDERID_LocalAppData));
		wstring programs = Utf8ToUtf16(KnownFolder(FOLDERID_ProgramFiles));
		wstring code = FirstFile({ local + L"\\Programs\\Microsoft VS Code\\Code.exe", programs + L"\\Microsoft VS Code\\Code.exe" });
		wstring npp = FirstFile({ programs + L"\\Notepad++\\notepad++.exe" });
		if (!code.empty()) {
			cmd = L"\"" + code + L"\" --goto \"" + path + L":" + number + L"\"";
		}
		else if (!npp.empty()) {
			cmd = L"\"" + npp + L"\" -n" + number + L" \"" + path + L"\"";
		}
		else {
			cmd = L"notepad.exe \"" + path + L"\"";
		}
	}
	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	if (CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		AllowSetForegroundWindow(pi.dwProcessId);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	else {
		Log(LOG_ERROR, "Unable to start the editor with " + Utf16ToUtf8(cmd) + ": " + LastErrorText());
	}
}

bool WinApp::OnConsoleKey(UINT vk) {
	// Enter runs the line, Up and Down step through earlier ones, Escape clears it
	if (vk == VK_RETURN) {
		int len = GetWindowTextLengthW(_hInput);
		vector<wchar_t> buffer((size_t)len + 1);
		GetWindowTextW(_hInput, buffer.data(), len + 1);
		wstring line(buffer.data());
		SetWindowTextW(_hInput, L"");
		if (!line.empty() && (_consoleHistory.empty() || _consoleHistory.back() != line)) {
			_consoleHistory.push_back(line);
		}
		_historyPos = _consoleHistory.size();
		_host->PostConsole(Utf16ToUtf8(line));
		return true;
	}
	if (vk == VK_UP || vk == VK_DOWN) {
		if (_consoleHistory.empty()) {
			return true;
		}
		if (vk == VK_UP && _historyPos > 0) {
			_historyPos--;
		}
		else if (vk == VK_DOWN && _historyPos < _consoleHistory.size()) {
			_historyPos++;
		}
		SetWindowTextW(_hInput, _historyPos < _consoleHistory.size() ? _consoleHistory[_historyPos].c_str() : L"");
		int len = GetWindowTextLengthW(_hInput);
		SendMessageW(_hInput, EM_SETSEL, len, len);
		return true;
	}
	if (vk == VK_ESCAPE) {
		SetWindowTextW(_hInput, L"");
		_historyPos = _consoleHistory.size();
		return true;
	}
	return false;
}

void WinApp::ConsoleState(bool more) {
	PostMessageW(_hWnd, WM_APP_CONSOLE, more ? 1 : 0, 0);
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
	// No hotkeys in headless mode, so tests can run next to a real copy.
	// Mouse hotkeys are the mouse hook's to catch.
	if (_options.Headless) {
		return true;
	}
	if (IsMouseTrigger(key)) {
		_mouseHotkeys[id] = make_pair(mods, key);
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
	if (!_options.Headless && _mouseHotkeys.erase(id) == 0) {
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

static UINT RemapKey(UINT vk, bool extended) {
	// The hook tells Enter from the numpad's Enter only by the extended flag
	return vk == VK_RETURN && extended ? vk | 0x10000 : vk;
}

void WinApp::SetRemaps(const vector<RemapBinding>& active) {
	// Work out the key codes now, so the hook only has to look them up.  The
	// hook sees the left and right modifiers, never the plain ones.
	_remaps.clear();
	HKL layout = GetKeyboardLayout(0);
	for (auto& rm : active) {
		KeyCode source, target;
		if (!LookupKey(rm.Source, layout, source) || source.ImpliedMods != 0) {
			if (_badRemaps.insert(rm.ID).second) {
				Log(LOG_ERROR, "Unable to remap " + rm.Source + ": it isn't a key of its own on this keyboard layout");
			}
			continue;
		}
		if (!rm.Target.empty() && (!LookupKey(rm.Target, layout, target) || target.ImpliedMods != 0)) {
			if (_badRemaps.insert(rm.ID).second) {
				Log(LOG_ERROR, "Unable to remap " + rm.Source + " to " + rm.Target + ": it isn't a key of its own on this keyboard layout");
			}
			continue;
		}
		RemapTarget to;
		to.VK = rm.Target.empty() ? 0 : target.VK;
		to.Extended = target.Extended;
		vector<UINT> keys;
		switch (source.VK) {
		case VK_CONTROL: keys = { VK_LCONTROL, VK_RCONTROL }; break;
		case VK_MENU: keys = { VK_LMENU, VK_RMENU }; break;
		case VK_SHIFT: keys = { VK_LSHIFT, VK_RSHIFT }; break;
		default:
			keys = { source.VK };
			if (rm.Source == "meta") {
				keys.push_back(VK_RWIN);
			}
		}
		for (UINT vk : keys) {
			_remaps[RemapKey(vk, source.Extended)] = to;
		}
	}
}

static void SendRemapKey(const RemapTarget& target, bool up) {
	INPUT input = { 0 };
	input.type = INPUT_KEYBOARD;
	input.ki.wVk = (WORD)target.VK;
	input.ki.wScan = (WORD)MapVirtualKeyW(target.VK, MAPVK_VK_TO_VSC_EX);
	input.ki.dwFlags = (target.Extended ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
	input.ki.dwExtraInfo = MICROKEYS_REMAP_TAG;
	SendInput(1, &input, sizeof(INPUT));
}

bool WinApp::OnRemapKey(const KBDLLHOOKSTRUCT* kb, bool down) {
	// A remapped key: send its target instead, and keep the key itself from
	// the program in front.  A key that went down remapped comes up the same
	// way, even if the remap changed while it was held.
	if (_remaps.empty() && _remapsDown.empty()) {
		return false;
	}
	UINT key = RemapKey(kb->vkCode, (kb->flags & LLKHF_EXTENDED) != 0);
	auto held = _remapsDown.find(key);
	if (!down) {
		if (held == _remapsDown.end()) {
			return false;
		}
		if (held->second.VK != 0) {
			SendRemapKey(held->second, true);
		}
		_remapsDown.erase(held);
		return true;
	}
	if (held == _remapsDown.end()) {
		auto it = _remaps.find(key);
		if (it == _remaps.end()) {
			return false;
		}
		held = _remapsDown.insert(make_pair(key, it->second)).first;
	}
	if (held->second.VK != 0) {
		SendRemapKey(held->second, false);
	}
	return true;
}

void WinApp::ReleaseRemaps() {
	// The hook is going, so nothing will see these keys come up
	for (auto& cur : _remapsDown) {
		if (cur.second.VK != 0) {
			SendRemapKey(cur.second, true);
		}
	}
	_remapsDown.clear();
	_mouseSwallowed.clear();
}

static int ModifiersDown() {
	int mods = 0;
	if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
		mods |= MOD_KEY_CTRL;
	}
	if (GetAsyncKeyState(VK_MENU) & 0x8000) {
		mods |= MOD_KEY_ALT;
	}
	if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
		mods |= MOD_KEY_SHIFT;
	}
	if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) {
		mods |= MOD_KEY_META;
	}
	return mods;
}

static void MaskMouseHotkey(int mods) {
	// The program in front never sees the click, so to it Alt or Win goes
	// down and up with nothing in between, which opens the menu bar or the
	// Start menu.  A Ctrl going down in between stops that: a Ctrl already
	// held goes up and down again, otherwise one is tapped.
	if ((mods & (MOD_KEY_ALT | MOD_KEY_META)) == 0) {
		return;
	}
	UINT vk = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) ? VK_RCONTROL : (GetAsyncKeyState(VK_LCONTROL) & 0x8000) ? VK_LCONTROL : 0;
	bool held = vk != 0;
	if (!held) {
		vk = VK_LCONTROL;
	}
	for (int i = 0; i < 2; i++) {
		INPUT input = { 0 };
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = (WORD)vk;
		input.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
		input.ki.dwFlags = (vk == VK_RCONTROL ? KEYEVENTF_EXTENDEDKEY : 0) | ((i == 0) == held ? KEYEVENTF_KEYUP : 0);
		input.ki.dwExtraInfo = MICROKEYS_INPUT_TAG;
		SendInput(1, &input, sizeof(INPUT));
	}
}

bool WinApp::OnMouseHotkey(UINT message, const MSLLHOOKSTRUCT* ms) {
	// A mouse button or wheel turn that's a hotkey runs its macro, and the
	// program under the pointer never sees it, nor the button coming up
	if (_mouseHotkeys.empty() && _mouseSwallowed.empty()) {
		return false;
	}
	string button;
	bool down = true;
	switch (message) {
	case WM_LBUTTONDOWN: case WM_LBUTTONUP:
		button = "mouseleft";
		down = message == WM_LBUTTONDOWN;
		break;
	case WM_RBUTTONDOWN: case WM_RBUTTONUP:
		button = "mouseright";
		down = message == WM_RBUTTONDOWN;
		break;
	case WM_MBUTTONDOWN: case WM_MBUTTONUP:
		button = "mousemiddle";
		down = message == WM_MBUTTONDOWN;
		break;
	case WM_XBUTTONDOWN: case WM_XBUTTONUP:
		button = HIWORD(ms->mouseData) == XBUTTON1 ? "mousex1" : "mousex2";
		down = message == WM_XBUTTONDOWN;
		break;
	case WM_MOUSEWHEEL:
		button = (short)HIWORD(ms->mouseData) > 0 ? "wheelup" : "wheeldown";
		break;
	case WM_MOUSEHWHEEL:
		button = (short)HIWORD(ms->mouseData) > 0 ? "wheelright" : "wheelleft";
		break;
	default:
		return false;
	}
	if (!down) {
		return _mouseSwallowed.erase(button) > 0;
	}
	int mods = ModifiersDown();
	for (auto& cur : _mouseHotkeys) {
		if (cur.second.first == mods && cur.second.second == button) {
			if (button.rfind("mouse", 0) == 0) {
				_mouseSwallowed.insert(button);
			}
			MaskMouseHotkey(mods);
			_triggers->OnHotkey(cur.first);
			return true;
		}
	}
	return false;
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
	if (!config.RunElevated) {
		_elevationTried = false;
	}
	bool recheck = !_configured || config.RunElevated != _config.RunElevated;
	_config = config;
	_configured = true;
	// Starting with Windows as administrator takes a scheduled task in place
	// of the Run key, which only a copy running as administrator can set up
	if (recheck && _platform.IsElevated() && RunAtLogin()) {
		SetRunAtLogin(true);
	}
	if (config.RunElevated && !_platform.IsElevated() && !_elevationTried) {
		_elevationTried = true;
		PostMessageW(_hWnd, WM_APP_ELEVATE, 0, 0);
	}
}

void WinApp::UpdateHooks() {
	// The keyboard hook only runs while there are hotstrings or remaps, the
	// mouse hook while there are hotstrings or mouse hotkeys, and title
	// changes are only watched when a binding cares about titles
	bool active = !_options.Headless && !_suspended;
	bool wantKeys = (_triggers->HasHotstrings() || _triggers->HasRemaps()) && active;
	if (wantKeys && _keyboardHook == NULL) {
		_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, _hInst, 0);
	}
	else if (!wantKeys && _keyboardHook != NULL) {
		UnhookWindowsHookEx(_keyboardHook);
		_keyboardHook = NULL;
		ReleaseRemaps();
	}
	bool wantMouse = (_triggers->HasHotstrings() || _triggers->HasMouseHotkeys()) && active;
	if (wantMouse && _mouseHook == NULL) {
		_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, _hInst, 0);
	}
	else if (!wantMouse && _mouseHook != NULL) {
		UnhookWindowsHookEx(_mouseHook);
		_mouseHook = NULL;
		_mouseSwallowed.clear();
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

	// Windows appearing, for windows.on_open.  The ones already on screen
	// when the watching starts aren't new.
	bool wantOpen = _triggers->WantsWindowEvent(WINDOW_OPEN) && !_options.Headless && !_suspended;
	if (wantOpen && _showHook == NULL) {
		_knownWindows.clear();
		EnumWindows(CollectWindow, (LPARAM)&_knownWindows);
		_showHook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_SHOW, NULL, WinEventProc, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
		_uncloakHook = SetWinEventHook(EVENT_OBJECT_UNCLOAKED, EVENT_OBJECT_UNCLOAKED, NULL, WinEventProc, 0, 0,
			WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
	}
	else if (!wantOpen && _showHook != NULL) {
		UnhookWinEvent(_showHook);
		_showHook = NULL;
		if (_uncloakHook != NULL) {
			UnhookWinEvent(_uncloakHook);
			_uncloakHook = NULL;
		}
		_knownWindows.clear();
	}
}

void WinApp::OnWinEvent(DWORD event, HWND hwnd, LONG idObject, LONG idChild) {
	if (event == EVENT_SYSTEM_FOREGROUND) {
		RefreshForeground(true);
		return;
	}
	if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
		return;
	}
	uint64_t handle = (uint64_t)(uintptr_t)hwnd;
	if (event == EVENT_OBJECT_NAMECHANGE) {
		if (hwnd == GetForegroundWindow()) {
			RefreshForeground(false);
		}
		if (_triggers->IsPendingWindow(handle)) {
			_triggers->OnWindowTitle(DescribeWindow(hwnd), GetTickCount64());
		}
	}
	else if (event == EVENT_OBJECT_SHOW || event == EVENT_OBJECT_UNCLOAKED) {
		OnWindowShown(hwnd);
	}
	else if (event == EVENT_OBJECT_DESTROY && _knownWindows.erase(hwnd) > 0) {
		_triggers->OnWindowClosed(handle);
	}
}

void WinApp::OnWindowShown(HWND hwnd) {
	// A window a person would call new: top level, able to take the focus,
	// not a menu or a tooltip, not hidden on another desktop, not seen before
	if (_knownWindows.count(hwnd) > 0 || GetAncestor(hwnd, GA_ROOT) != hwnd || !IsWindowVisible(hwnd)) {
		return;
	}
	LONG style = GetWindowLongW(hwnd, GWL_STYLE);
	LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
	if (((exStyle & WS_EX_TOOLWINDOW) && !(exStyle & WS_EX_APPWINDOW)) || (exStyle & WS_EX_NOACTIVATE)) {
		return;
	}
	if (GetWindow(hwnd, GW_OWNER) != NULL && (style & WS_CAPTION) != WS_CAPTION) {
		return;
	}
	DWORD cloaked = 0;
	if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0) {
		return;
	}
	_knownWindows.insert(hwnd);
	_triggers->OnWindowOpened(DescribeWindow(hwnd), GetTickCount64());
}

void WinApp::RefreshForeground(bool newWindow) {
	// Keep a copy of the foreground window's details, since the keyboard hook
	// can't afford to look them up on every key
	HWND hwnd = GetForegroundWindow();
	_foreground = DescribeWindow(hwnd);
	_foregroundLayout = GetKeyboardLayout(GetWindowThreadProcessId(hwnd, NULL));
	if (newWindow) {
		_triggers->OnForeground(_foreground);
		// MicroKeys' own windows, its dialogs say, aren't what scripts mean
		if (_foreground.Pid != GetCurrentProcessId()) {
			_triggers->OnActivated(_foreground);
		}
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

static void MaskAlt(UINT mods) {
	// Windows keeps the hotkey's own key from the program in front, so if Alt
	// comes up while Ctrl is still down, Windows 11's Notepad (WinUI) sees
	// Ctrl+Alt with nothing in between and shows its access keys, and the
	// macro's typing goes to its menus.  A Ctrl going down while Alt is still
	// down stops that, so let go of the Ctrl being held and press it again.
	// Keys that do nothing (VK 0xE8, which AutoHotkey uses, or VK_NONAME)
	// don't stop it.  A Ctrl already let go is fine as it is.
	if ((mods & (MOD_CONTROL | MOD_ALT)) != (MOD_CONTROL | MOD_ALT)) {
		return;
	}
	UINT vk = (GetAsyncKeyState(VK_RCONTROL) & 0x8000) ? VK_RCONTROL : (GetAsyncKeyState(VK_LCONTROL) & 0x8000) ? VK_LCONTROL : 0;
	if (vk == 0) {
		return;
	}
	for (int i = 0; i < 2; i++) {
		INPUT input = { 0 };
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = (WORD)vk;
		input.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
		input.ki.dwFlags = (vk == VK_RCONTROL ? KEYEVENTF_EXTENDEDKEY : 0) | (i == 0 ? KEYEVENTF_KEYUP : 0);
		input.ki.dwExtraInfo = MICROKEYS_INPUT_TAG;
		SendInput(1, &input, sizeof(INPUT));
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
	string error;
	_platform.SendKeysNow(actions, error);
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

static wstring UserSid() {
	// The signed in user, as a SID string for the scheduled task
	wstring ret;
	HANDLE token = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		return ret;
	}
	DWORD size = 0;
	GetTokenInformation(token, TokenUser, NULL, 0, &size);
	vector<BYTE> buffer(size);
	LPWSTR text = NULL;
	if (size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size) &&
		ConvertSidToStringSidW(((TOKEN_USER*)buffer.data())->User.Sid, &text)) {
		ret = text;
		LocalFree(text);
	}
	CloseHandle(token);
	return ret;
}

static wstring TaskName() {
	// Scheduled tasks are shared by everyone on the machine
	wchar_t user[256];
	DWORD size = 256;
	return GetUserNameW(user, &size) ? L"MicroKeys (" + wstring(user) + L")" : L"MicroKeys";
}

static wstring XmlEscape(const wstring& text) {
	wstring ret;
	for (wchar_t c : text) {
		switch (c) {
		case '&': ret += L"&amp;"; break;
		case '<': ret += L"&lt;"; break;
		case '>': ret += L"&gt;"; break;
		default: ret += c;
		}
	}
	return ret;
}

static bool LoginTask(const wstring& exe, bool create, bool remove) {
	// Look for, create, or remove the scheduled task that starts MicroKeys
	// as administrator when the user signs in, without asking
	HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	bool ret = false;
	{
		ComPtr<ITaskService> service;
		ComPtr<ITaskFolder> folder;
		VARIANT empty;
		VariantInit(&empty);
		BSTR root = SysAllocString(L"\\");
		BSTR name = SysAllocString(TaskName().c_str());
		if (SUCCEEDED(CoCreateInstance(__uuidof(TaskScheduler), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service))) &&
			SUCCEEDED(service->Connect(empty, empty, empty, empty)) && SUCCEEDED(service->GetFolder(root, &folder))) {
			if (create) {
				wstring sid = UserSid();
				wstring xml =
					L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
					L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
					L"<RegistrationInfo><Description>Starts MicroKeys as administrator when you sign in</Description></RegistrationInfo>"
					L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + sid + L"</UserId></LogonTrigger></Triggers>"
					L"<Principals><Principal id=\"Author\"><UserId>" + sid + L"</UserId><LogonType>InteractiveToken</LogonType>"
					L"<RunLevel>HighestAvailable</RunLevel></Principal></Principals>"
					L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
					L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
					L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</Priority></Settings>"
					L"<Actions Context=\"Author\"><Exec><Command>" + XmlEscape(exe) + L"</Command></Exec></Actions>"
					L"</Task>";
				BSTR text = SysAllocString(xml.c_str());
				ComPtr<IRegisteredTask> task;
				ret = SUCCEEDED(folder->RegisterTask(name, text, TASK_CREATE_OR_UPDATE, empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &task));
				SysFreeString(text);
			}
			else if (remove) {
				ret = SUCCEEDED(folder->DeleteTask(name, 0));
			}
			else {
				ComPtr<IRegisteredTask> task;
				ret = SUCCEEDED(folder->GetTask(name, &task));
			}
		}
		SysFreeString(root);
		SysFreeString(name);
	}
	if (SUCCEEDED(init)) {
		CoUninitialize();
	}
	return ret;
}

bool WinApp::RunAtLogin() {
	wchar_t value[1024];
	DWORD size = sizeof(value);
	return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"MicroKeys",
		RRF_RT_REG_SZ, NULL, value, &size) == ERROR_SUCCESS || LoginTask(L"", false, false);
}

void WinApp::SetRunAtLogin(bool enable) {
	// Start through the launcher when there is one, so updates are picked up.
	// To start as administrator without asking, a scheduled task does it
	// instead of the Run key; only a copy running as administrator can set
	// that up, and the others leave it alone.
	wstring exe = Utf8ToUtf16(_installDir + "\\MicroKeys.exe");
	if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
		exe = Utf8ToUtf16(_appDir + "\\MicroKeysApp.exe");
	}
	bool elevated = _platform.IsElevated();
	bool useTask = enable && _config.RunElevated && elevated;
	if (useTask && !LoginTask(exe, true, false)) {
		Log(LOG_ERROR, "Unable to set up the scheduled task that starts MicroKeys as administrator; it will ask each time instead");
		useTask = false;
	}
	if (!useTask && elevated) {
		LoginTask(L"", false, true);
	}
	HKEY key;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
		return;
	}
	if (enable && !useTask) {
		wstring cmd = L"\"" + exe + L"\"";
		RegSetValueExW(key, L"MicroKeys", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
	}
	else {
		RegDeleteValueW(key, L"MicroKeys");
	}
	RegCloseKey(key);
}

static wstring QuoteArg(const wstring& arg) {
	// Quoted so CommandLineToArgvW gives it back as it was: backslashes are
	// only special when they come before a quote
	wstring ret = L"\"";
	size_t slashes = 0;
	for (wchar_t c : arg) {
		if (c == '\\') {
			slashes++;
			continue;
		}
		ret.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
		slashes = 0;
		ret += c;
	}
	ret.append(slashes * 2, '\\');
	return ret + L"\"";
}

void WinApp::RestartElevated() {
	// Start this version again as administrator, and hand over to it the way
	// an update does.  Windows may ask the user to allow it first, which
	// takes as long as it takes, so the asking happens on a thread of its own
	// rather than holding up the hooks; WM_APP_ELEVATE comes back with how
	// it went.
	wchar_t exe[MAX_PATH * 2];
	GetModuleFileNameW(NULL, exe, MAX_PATH * 2);
	wstring path = exe;
	wstring args = L"/elevated " + to_wstring(GetCurrentProcessId());
	// The new copy uses the same folders as this one
	const pair<const wchar_t*, string> options[] = {
		{ L"--scripts", _options.ScriptDir }, { L"--data", _options.DataDir }, { L"--log", _options.LogFile },
	};
	for (auto& cur : options) {
		if (!cur.second.empty()) {
			args += L" " + wstring(cur.first) + L" " + QuoteArg(Utf8ToUtf16(cur.second));
		}
	}
	HWND hwnd = _hWnd;
	thread([hwnd, path, args] {
		CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		SHELLEXECUTEINFOW info = { sizeof(info) };
		info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
		info.lpVerb = L"runas";
		info.lpFile = path.c_str();
		info.lpParameters = args.c_str();
		info.nShow = SW_SHOWNORMAL;
		DWORD err = ShellExecuteExW(&info) ? ERROR_SUCCESS : GetLastError();
		CoUninitialize();
		PostMessageW(hwnd, WM_APP_ELEVATE, 1, err);
	}).detach();
}

void WinApp::ShowLog() {
	ShowWindow(_hWnd, IsIconic(_hWnd) ? SW_RESTORE : SW_SHOW);
	SetForegroundWindow(_hWnd);
	AppendLogText();
	CHARRANGE end = { -1, -1 };
	SendMessageW(_hEdit, EM_EXSETSEL, 0, (LPARAM)&end);
	SendMessageW(_hEdit, WM_VSCROLL, SB_BOTTOM, 0);
	SendMessageW(_hEdit, WM_HSCROLL, SB_LEFT, 0);
	SetFocus(_hInput);
}

void WinApp::ShowTrayMenu(int x, int y) {
	HMENU menu = CreatePopupMenu();
	AppendMenuW(menu, MF_STRING, ID_SHOWLOG, L"&Show log");
	AppendMenuW(menu, MF_STRING, ID_EDITSCRIPTS, L"&Open script folder");
	AppendMenuW(menu, MF_STRING, ID_FILE_RELOADPYTHON, L"&Reload scripts");
	AppendMenuW(menu, MF_STRING | (_triggers->IsPaused() ? MF_CHECKED : 0), ID_PAUSE, L"&Pause");
	AppendMenuW(menu, MF_STRING | (_host->IsBusy() ? 0 : MF_GRAYED), ID_STOPMACRO, L"S&top running macro");
	AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
	AppendMenuW(menu, MF_STRING | (RunAtLogin() ? MF_CHECKED : 0), ID_RUNATLOGIN, L"Start with &Windows");
	AppendMenuW(menu, MF_STRING | (_config.RunElevated ? MF_CHECKED : 0), ID_RUNELEVATED, L"Run as ad&ministrator");
	AppendMenuW(menu, MF_STRING, ID_OPENSETTINGS, L"Edit se&ttings");
	AppendMenuW(menu, MF_STRING, ID_INSTALLPACKAGES, L"Install Python pac&kages...");
	AppendMenuW(menu, MF_STRING, ID_EXPORT, L"&Export scripts and settings...");
	AppendMenuW(menu, MF_STRING, ID_IMPORT, L"&Import scripts and settings...");
	HMENU network = CreatePopupMenu();
	AppendMenuW(network, MF_STRING, ID_NET_NEWTOKEN, L"&Generate a pairing token...");
	AppendMenuW(network, MF_STRING, ID_NET_ENTERTOKEN, L"&Enter a pairing token...");
	AppendMenuW(network, MF_STRING, ID_NET_OFF, L"&Turn networking off...");
	AppendMenuW(menu, MF_POPUP, (UINT_PTR)network, L"&Networking");
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
		_host->Paused = _triggers->IsPaused();
		Log(LOG_INFO, _triggers->IsPaused() ? "Paused: hotkeys, hotstrings, timers, and events are off" : "Resumed");
		UpdateTray();
		break;
	case ID_INSTALLPACKAGES:
		_host->PostAction("install-packages");
		break;
	case ID_EXPORT:
		_host->PostAction("export");
		break;
	case ID_IMPORT:
		_host->PostAction("import");
		break;
	case ID_RUNELEVATED:
		_host->PostAction("toggle-elevated");
		break;
	case ID_NET_NEWTOKEN:
		_host->PostAction("net-new-token");
		break;
	case ID_NET_ENTERTOKEN:
		_host->PostAction("net-enter-token");
		break;
	case ID_NET_OFF:
		_host->PostAction("net-off");
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
	if (_clipboardListener) {
		RemoveClipboardFormatListener(_hWnd);
		_clipboardListener = false;
	}
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
	if (_clipboardListener) {
		RemoveClipboardFormatListener(_hWnd);
		_clipboardListener = false;
	}
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

	case WM_APP_ELEVATE:
		// Asked to restart as administrator (0), or how the asking went (1)
		if (_suspended || _exiting) {
			return 0;
		}
		if (wParam == 0) {
			RestartElevated();
		}
		else if (lParam == ERROR_SUCCESS) {
			Log(LOG_INFO, "Starting again as administrator");
			ExitApp();
		}
		else if (lParam == ERROR_CANCELLED) {
			Log(LOG_INFO, "Running without administrator rights, since Windows wasn't allowed to give them");
		}
		else {
			Log(LOG_ERROR, "Unable to start as administrator: " + LastErrorText((DWORD)lParam));
		}
		return 0;

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

	case WM_APP_OVERLAY: {
		unique_ptr<pair<string, int>> overlay((pair<string, int>*)lParam);
		_overlay->Show(Utf8ToUtf16(overlay->first), overlay->second);
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

	case WM_APP_CONSOLE:
		SetWindowTextW(_hPrompt, wParam != 0 ? L"..." : L">>>");
		return 0;

	case WM_CLIPBOARDUPDATE:
		// What MicroKeys puts there itself doesn't count
		if (!_suspended && GetClipboardOwner() != _hWnd) {
			_host->PostFire(FIRE_CLIPBOARD, 0, "");
		}
		return 0;

	case WM_NOTIFY: {
		ENLINK* link = (ENLINK*)lParam;
		if (link->nmhdr.hwndFrom == _hEdit && link->nmhdr.code == EN_LINK && link->msg == WM_LBUTTONUP) {
			vector<wchar_t> buffer((size_t)(link->chrg.cpMax - link->chrg.cpMin) + 1);
			TEXTRANGEW range = { link->chrg, buffer.data() };
			SendMessageW(_hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&range);
			OpenLink(buffer.data());
			return 1;
		}
		break;
	}

	case WM_CTLCOLORSTATIC:
		if ((HWND)lParam == _hPrompt) {
			SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
			SetTextColor((HDC)wParam, LOG_CONSOLE_COLOR);
			return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
		}
		break;

	case WM_SETFOCUS:
		SetFocus(_hInput);
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
			MaskAlt(LOWORD(lParam));
			Abort();
		}
		else if (!PassAltGr(LOWORD(lParam), HIWORD(lParam))) {
			MaskAlt(LOWORD(lParam));
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
