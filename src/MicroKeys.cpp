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
	return wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().from_bytes(str);
}

string WStrToStr(wstring wstr) {
	return wstring_convert<codecvt_utf8<wchar_t>, wchar_t>().to_bytes(wstr);
}

bool TestLogEnabled() {
	return GetEnvironmentVariableW(L"MICROKEYS_LOG").length() > 0;
}

void TestLog(string msg) {
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
	return _keys;
}

HWND GetMainWindow() {
	return _hWndMain;
}

void DoEvents() {
	if (_hWndEdit == NULL) {
		return;
	}

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
	LPWSTR temp = NULL;
	int len = LoadStringW(hInstance, id, (LPWSTR)&temp, 0);
	if (len > 0) {
		return wstring{temp, static_cast<size_t>(len)};
	}
	return L"";
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
	_title = LoadStringW(hInstance, IDS_APP_TITLE);
	_windowClass = LoadStringW(hInstance, IDC_MICROKEYS);

	int argc = 0;
	LPWSTR*argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argc == 2) {
		HWND hWnd = FindWindow(_windowClass.c_str(), _title.c_str());
		COPYDATASTRUCT cds = { 0 };
		string temp = WStrToStr(argv[1]);
		cds.dwData = CDS_INVOKE_MACRO;
		cds.cbData = (DWORD)temp.length();
		cds.lpData = (void*)temp.c_str();
		SendMessage(hWnd, WM_COPYDATA, 0, (LPARAM)&cds);
		return 0;
	}

	HANDLE hMutex = CreateMutex(NULL, TRUE, MICROKEYS_MUTEX);
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		HWND hWnd = FindWindow(_windowClass.c_str(), _title.c_str());
		ShowWindow(hWnd, SW_SHOWNORMAL);
		SetForegroundWindow(hWnd);
		return 0;
	}

	wstring run = GetEnvironmentVariableW(L"MICROKEYS_RUN");
	if (run.length() > 0) {
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

	MyRegisterClass(hInstance);

	if (!InitInstance(hInstance, nCmdShow)) {
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MICROKEYS));
	MSG msg;

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
	return wwinmain(getmodulehandle(null), null, null, sw_show);
}
#endif

ATOM MyRegisterClass(HINSTANCE hInstance) {
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

	LoadPython();

	return TRUE;
}

void LogMessage(string msg) {
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
	switch (message) {
	case WM_HOTKEY:
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
	switch (message) {
	case WM_INITDIALOG:
	{
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

wstring GetEnvironmentVariableW(wstring name) {
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
	for (auto& key : _keys) {
		if (key.VK != 0) {
			UnregisterHotKey(_hWndMain, key.ID);
		}
	}
	_keys.clear();
	// TODO: Need to clean up memory from any previous runs.

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

	int ret = run_micro_python(data);
	free(data);
	if (ret != 0) {
		stringstream ss;
		ss << "Call returned: " << ret;
		LogMessage(ss.str());
	}
}
