#include "framework.h"
#include "MicroKeys.h"

HINSTANCE _hInst = NULL;
wstring _title = L"";
wstring _windowClass = L"";
HWND _hWndMain = NULL;
HWND _hWndEdit = NULL;
bool _handleHotkeys = true;
vector<KeyData> _keys;

wstring StrToWStr(string str) {
	// Helper to convert a string to a wstring
	return wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().from_bytes(str);
}

string WStrToStr(wstring wstr) {
	// Helper to convert a wstring to a string
	return wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(wstr);
}

bool TestLogEnabled() {
	// Returns true if the environment variable 'MICROKEYS_LOG' is set, meaning
	// we should be logging all activity instead of doing it
	return GetEnvironmentVariableW(L"MICROKEYS_LOG").length() > 0;
}

void TestLog(string msg) {
	// Logs a message to the file that 'MICROKEYS_LOG' points to.  It is
	// invalid to call this if MICROKEYS_LOG is not set.
	wstring filename = GetEnvironmentVariableW(L"MICROKEYS_LOG");
	HANDLE hFile = CreateFile(filename.c_str(), FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != NULL && hFile != INVALID_HANDLE_VALUE)
	{
		msg += "\r\n";
		SetFilePointer(hFile, 0, NULL, FILE_END);
		WriteFile(hFile, msg.c_str(), (DWORD)(msg.length()), NULL, NULL);
		CloseHandle(hFile);
	}
}

vector<KeyData>& GetKeys() {
	// Helper to return the list of registered keys
	return _keys;
}

HWND GetMainWindow() {
	// Helper to return a handle to the main window
	return _hWndMain;
}

void DoEvents() {
	// Loop through and process any waiting messages.  This can be called
	// during a long running macro to keep the main window from "freezing up",
	// though care should be taken, since this can trigger recursing in some cases
	if (_hWndEdit == NULL) {
		return;
	}

	// During this message pumping, don't allow any hotkeys to trigger
	_handleHotkeys = false;
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
		if (GetMessage(&msg, NULL, 0, 0)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			break;
		}
	}
	_handleHotkeys = true;
}

wstring LoadStringW(HINSTANCE hInstance, UINT id) {
	// Helper to call Win32's LoadStringW, dealing with the string alloc
	LPWSTR temp = NULL;
	int len = LoadStringW(hInstance, id, (LPWSTR)&temp, 0);
	if (len > 0) {
		return wstring{ temp, static_cast<size_t>(len) };
	}
	return L"";
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
	// Main entyr point
	_title = LoadStringW(hInstance, IDS_APP_TITLE);
	_windowClass = LoadStringW(hInstance, IDC_MICROKEYS);

	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argc == 2) {
		// We have an arg treat it as the name of a macro

		HANDLE hMutex = CreateMutex(NULL, TRUE, MICROKEYS_MUTEX);
		if (GetLastError() == ERROR_ALREADY_EXISTS) {
			// Ok, another instance exists (or, should exist), pass the name of
			// the macro off to it to run
			HWND hWnd = FindWindow(_windowClass.c_str(), _title.c_str());
			COPYDATASTRUCT cds = { 0 };
			string temp = WStrToStr(argv[1]);
			cds.dwData = CDS_INVOKE_MACRO;
			cds.cbData = (DWORD)temp.length();
			cds.lpData = (void*)temp.c_str();
			SendMessage(hWnd, WM_COPYDATA, 0, (LPARAM)&cds);
		}
		else {
			// We're the only instance, go ahead and load the macro code
			// and run the desired macro
			LoadPython();
			istringstream run_ss(WStrToStr(argv[1]));
			string cur;
			while (getline(run_ss, cur, ',')) {
				for (auto& key : _keys) {
					if (key.Name == cur) {
						run_fun(key.PythonFunction);
						break;
					}
				}
			}
		}
		return 0;
	}

	// If we get here, we have no args, so this is a normal start up
	HANDLE hMutex = CreateMutex(NULL, TRUE, MICROKEYS_MUTEX);
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		// Another instance exists, so find it and bring it to the foreground
		HWND hWnd = FindWindow(_windowClass.c_str(), _title.c_str());
		ShowWindow(hWnd, SW_SHOWNORMAL);
		SetForegroundWindow(hWnd);
		return 0;
	}

	wstring run = GetEnvironmentVariableW(L"MICROKEYS_RUN");
	if (run.length() > 0) {
		// The environment variable 'MICROKEYS_RUN' is set, so just load the
		// macro code, and run whatever functions we were told to run
		// This environment variable logic mostly exists for test code.
		LoadPython();
		istringstream run_ss(WStrToStr(run));
		string cur;
		while (getline(run_ss, cur, ',')) {
			for (auto& key : _keys) {
				if (key.Name == cur) {
					run_fun(key.PythonFunction);
					break;
				}
			}
		}
		return 0;
	}

	// And if we got here, we should just start normally, so do the normal Win32
	// GUI stuff.
	MyRegisterClass(hInstance);

	if (!InitInstance(hInstance, nCmdShow)) {
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MICROKEYS));
	MSG msg;

	// And our main message loop
	while (GetMessage(&msg, nullptr, 0, 0)) {
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return (int)msg.wParam;
}

#if 0
extern "C" int main() {
	// Place holder for a CLI version of this build, not used much anymore
	return wwinmain(getmodulehandle(null), null, null, sw_show);
}
#endif

ATOM MyRegisterClass(HINSTANCE hInstance) {
	// Standard boilerplate to register a window class
	WNDCLASSEXW wcex = { 0 };

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MICROKEYS));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_MICROKEYS);
	wcex.lpszClassName = _windowClass.c_str();

	return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
	// Create the main window, and the child edit control
	_hInst = hInstance;

	_hWndMain = CreateWindowW(
		_windowClass.c_str(), _title.c_str(), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		500, 500,
		nullptr, nullptr, hInstance, nullptr);

	if (!_hWndMain) {
		return FALSE;
	}

	_hWndEdit = CreateWindowEx(WS_EX_CLIENTEDGE, _T("Edit"), _T(""),
		WS_CHILD | WS_VISIBLE | ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_HSCROLL, 0, 20, 200,
		200, _hWndMain, NULL, NULL, NULL);

	SendMessage(_hWndEdit, EM_LIMITTEXT, 0x7FFFFFFE, 0);
	HFONT hFont = CreateFont(
		-MulDiv(10, GetDeviceCaps(GetDC(_hWndEdit), LOGPIXELSY), 72),
		0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, ANSI_CHARSET,
		OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, TEXT("Courier New"));
	SendMessage(_hWndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

	Resize(_hWndMain);

	ShowWindow(_hWndMain, nCmdShow);
	UpdateWindow(_hWndMain);

	// Load the python code
	LoadPython();
	// And start a worker to keep an eye on the file
	CreateThread(NULL, 0, FileWatcher, NULL, 0, NULL);

	return TRUE;
}

unsigned long long GetCodeLastWriteTime() {
	// Helper to return the last write time of the code file
#ifdef _DEBUG
	wstring code_filename = L"macro\\MicroKeys.py";
#else
	wstring code_filename = L"MicroKeys.py";
#endif
	wstring other_filename = GetEnvironmentVariableW(L"MICROKEYS_SOURCE");

	if (other_filename.size() > 0) {
		code_filename = other_filename;
	}

	HANDLE hFile = CreateFile(
		code_filename.c_str(),
		GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

	if (hFile == INVALID_HANDLE_VALUE) {
		return 0;
	}

	FILETIME lastWrite;
	if (!GetFileTime(hFile, NULL, NULL, &lastWrite)) {
		CloseHandle(hFile);
		return 0;
	}

	ULARGE_INTEGER temp;
	temp.HighPart = lastWrite.dwHighDateTime;
	temp.LowPart = lastWrite.dwLowDateTime;

	CloseHandle(hFile);

	return temp.QuadPart;
}

DWORD WINAPI FileWatcher(LPVOID lpParameter) {
	// Keep an eye on the macro data file, if the write time of it changes
	// trigger a reload.  This runs in a seperate thread since it's a long
	// running thing

#ifdef _DEBUG
	wstring code_path = L"macro";
#else
	wstring code_path = L".";
#endif

	// Open a handle to the directory the file is in
	code_path = GetFullPathNameW(code_path);
	HANDLE dir = CreateFileW(
		code_path.c_str(),
		FILE_LIST_DIRECTORY,
		FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	// TODO: Handle other source filenames, they may be in a different folder
	const wchar_t* code = L"MicroKeys.py";
	DWORD code_len = (DWORD)wcslen(code);

	if (dir == INVALID_HANDLE_VALUE) {
		return 0;
	}

	// The current write time of the file
	unsigned long long lastWrite = GetCodeLastWriteTime();

	unsigned char buffer[sizeof(FILE_NOTIFY_INFORMATION) * 128];
	DWORD BytesReturned;
	// Just keep looping through any directory changes
	while (ReadDirectoryChangesW(
		dir, buffer, sizeof(buffer),
		TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE,
		&BytesReturned, NULL, NULL)) {

		FILE_NOTIFY_INFORMATION* cur = (FILE_NOTIFY_INFORMATION*)buffer;
		do {
			if (_wcsnicmp(cur->FileName, code, (cur->FileNameLength / sizeof(WCHAR)) > code_len ? code_len : (cur->FileNameLength / sizeof(WCHAR))) == 0) {
				// We've been told about the file we care about
				unsigned long long newLastWrite = GetCodeLastWriteTime();
				if (newLastWrite != lastWrite) {
					// And it has a different write time
					lastWrite = newLastWrite;
					// Allow for some "bounce" by waiting a bit before triggering any action
					KillTimer(_hWndMain, TIMER_SOURCE_CHANGED);
					SetTimer(_hWndMain, TIMER_SOURCE_CHANGED, 250, NULL);
				}
			}
			cur = (FILE_NOTIFY_INFORMATION*)(((unsigned char*)cur) + cur->NextEntryOffset);
		} while (cur->NextEntryOffset);
	}

	return 0;
}

void LogMessage(string msg) {
	// Helper to log a message to the edit box.  This helper will
	// add a CRLF to the string that's passed in
	if (_hWndEdit == NULL) {
		return;
	}
	msg += "\r\n";
	int index = GetWindowTextLength(_hWndEdit);
	// Escape hatch for the input box getting too big
	if (SendMessage(_hWndEdit, WM_GETTEXTLENGTH, 0, 0) > 10 * 1024 * 1024) {
		SendMessage(_hWndEdit, WM_SETTEXT, (WPARAM)0, (LPARAM)_T(""));
	}
	SendMessage(_hWndEdit, EM_SETSEL, (WPARAM)index, (LPARAM)index);
	SendMessageA(_hWndEdit, EM_REPLACESEL, 0, (LPARAM)msg.c_str());

#if 0
	// Some code to write all of the data to a log file, can be useful to debug issues
	HANDLE hFile = CreateFile(_T("MicroKeys.log"), FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile != NULL && hFile != INVALID_HANDLE_VALUE)
	{
		SYSTEMTIME st = { 0 };
		GetSystemTime(&st);

		char header[50] = { 0 };
		sprintf_s(header, "%d-%02d-%02d %2d:%02d:%02d.%04d: ",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

		SetFilePointer(hFile, 0, NULL, FILE_END);
		WriteFile(hFile, header, (DWORD)(strlen(header)), NULL, 0);
		WriteFile(hFile, msg, (DWORD)(strlen(msg)), NULL, NULL);
		CloseHandle(hFile);
	}
#endif
}

void Resize(HWND hWnd) {
	// Called whenever we need to resize the child controls.  For now this
	// is fairly simple, just make the edit control take up all available space
	RECT rt;
	GetClientRect(hWnd, &rt);
	MoveWindow(_hWndEdit,
		rt.left,
		rt.top,
		rt.right - rt.left,
		rt.bottom - rt.top,
		TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	// Our main window procedure

	switch (message) {
	case WM_TIMER:
		switch (wParam) {
		case TIMER_SOURCE_CHANGED:
			// We were told to reload the data file, so do just that
			KillTimer(_hWndMain, TIMER_SOURCE_CHANGED);
			LogMessage("Detected code change, reloading");
			LoadPython();
			break;
		}
		break;

	case WM_HOTKEY:
		// A hotkey was pressed, make sure it's real, and run the code for it
		if (_handleHotkeys && (int)wParam <= _keys.size() && wParam > 0) {
			stringstream ss;
			ss << "Keypress of " << _keys[wParam - 1].Description << " detected";
			LogMessage(ss.str());
			WaitForKeyboard();
			run_fun(_keys[wParam - 1].PythonFunction);
		}
		return 0;

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case ID_FILE_RELOADPYTHON:
			LoadPython();
			break;
		case IDM_ABOUT:
			DialogBox(_hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_SIZE:
		Resize(hWnd);
		break;

	case WM_COPYDATA:
	{
		// This happens when we're told to run a macro by name, so get the string
		// for this macro, and look for it
		COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
		if (cds->dwData == CDS_INVOKE_MACRO) {
			string target((char*)cds->lpData, cds->cbData);
			for (auto& key : _keys) {
				if (key.Name == target) {
					stringstream ss;
					ss << "Macro '" << key.Name << "' called";
					LogMessage(ss.str());
					WaitForKeyboard();
					run_fun(key.PythonFunction);
					break;
				}
			}
		}
	}
	break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
	// Boilerplat about dialog

	switch (message) {
	case WM_INITDIALOG:
	{
		// Pull out the versions and show them
		SendDlgItemMessage(hDlg, IDC_VERSION, WM_SETTEXT, 0, (LPARAM)_T("Version ") VERSION_TEXT_T _T(", Copyright \xA9 ") VERSION_YEAR_T);
		wstringstream ss;
		ss << L"Includes MicroPython v" << StrToWStr(get_micropython_ver());
		SendDlgItemMessage(hDlg, IDC_MP_VERSION, WM_SETTEXT, 0, (LPARAM)ss.str().c_str());
	}
	return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}

	return (INT_PTR)FALSE;
}

void WaitForKeyboard() {
	// Helper to wait for the keyboard state to show no keys pressed down
	// This is called before most macros so that, for instance, the user holding down "Alt"
	// after releasing the "Z" while pressing "Alt-Z" to trigger that macro doesn't cause
	// the macro to hit "Alt-<whatever>" when it tries to press a normal key.
	if (_hWndMain == NULL) {
		return;
	}
	for (int bail = 0; bail < 50; bail++) {
		bool hit = false;
		for (int vk = 0x08; vk <= 0xC0; vk++) {
			if ((GetAsyncKeyState(vk) & 0x8000) == 0x8000) {
				hit = true;
				break;
			}
		}
		if (hit) {
			Sleep(100);
		}
		else {
			break;
		}
	}
}

wstring GetFullPathNameW(wstring filename) {
	// Wrapper around Win32 GetFullPathNameW to handle the string alloc
	DWORD len = GetFullPathNameW(filename.c_str(), 0, NULL, NULL);
	if (len > 0) {
		len++;
		vector<wchar_t> buffer(len);
		len = GetFullPathNameW(filename.c_str(), len, buffer.data(), NULL);
		if (len > 0) {
			return wstring(buffer.data(), len);
		}
	}
	return L"";
}

wstring GetEnvironmentVariableW(wstring name) {
	// Wrapper around Win32 GetEnvironmentVariableW to handle the string alloc
	DWORD len = GetEnvironmentVariableW(name.c_str(), NULL, 0);
	if (len > 0) {
		len++;
		vector<wchar_t> buffer(len);
		len = GetEnvironmentVariableW(name.c_str(), buffer.data(), len);
		if (len > 0) {
			return wstring(buffer.data(), len);
		}
	}
	return L"";
}

void LoadPython() {
	// Main worker to load and compile the python code

	// First off, de-register any hotkeys from any previous lods
	for (auto& key : _keys) {
		if (key.VK != 0) {
			UnregisterHotKey(_hWndMain, key.ID);
		}
	}
	_keys.clear();
	// TODO: Need to clean up memory from any previous runs.

	// Find the source code file and open it up
#ifdef _DEBUG
	wstring code_filename = L"macro\\MicroKeys.py";
#else
	wstring code_filename = L"MicroKeys.py";
#endif
	wstring other_filename = GetEnvironmentVariableW(L"MICROKEYS_SOURCE");
	if (other_filename.size() > 0) {
		code_filename = other_filename;
	}

	HANDLE hFile = CreateFile(
		code_filename.c_str(),
		GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		stringstream ss;
		using convert_type = codecvt_utf8<wchar_t>;
		wstring_convert<convert_type, wchar_t> converter;

		ss << "Unable to open " << converter.to_bytes(code_filename);
		LogMessage(ss.str());
		return;
	}

	// Just load the entire contents into memory
	DWORD len = GetFileSize(hFile, NULL);
	char* data = (char*)malloc((size_t)len + 1);
	if (data == NULL) {
		LogMessage("Failure reading data");
		return;
	}

	memset(data, 0, (size_t)len + 1);
	if (!ReadFile(hFile, data, len, NULL, NULL)) {
		LogMessage("Unable to read data");
		return;
	}

	// And call the MicroPython helper to run this code.  This helper
	// will compile it, and run it, which will trigger decorators in the
	// Python code to run which will save their macro's functions for us
	// to use later on
	int ret = run_micro_python(data);

	// All done, some clean up
	free(data);
	if (ret != 0) {
		// Hopefully the exception gave some clue what went wrong, but in case
		// it didn't, at least make it clear that something failed
		stringstream ss;
		ss << "ERROR: Call returned: " << ret;
		LogMessage(ss.str());
	}
}
