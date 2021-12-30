#include "framework.h"
#include "MicroKeys.h"

HINSTANCE _hInst = NULL;
wstring _title = L"";
wstring _windowClass = L"";
HWND _hWndMain = NULL;
HWND _hWndEdit = NULL;
bool _handleHotkeys = true;
vector<KeyData> _keys;

vector<KeyData>& GetKeys() {
	return _keys;
}

HWND GetMainWindow() {
	return _hWndMain;
}

void DoEvents() {
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
		char temp[1000] = { 0 };
		WideCharToMultiByte(CP_UTF8, 0, argv[1], (int)wcslen(argv[1]), temp, 1000, NULL, NULL);
		cds.dwData = CDS_INVOKE_MACRO;
		cds.cbData = (int)strlen(temp) + 1;
		cds.lpData = temp;
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
			ss << "Keypress of " << _keys[wParam - 1].Description << " detected\r\n";
			LogMessage(ss.str());
			WaitForKeyboard();
			run_fun(_keys[wParam - 1].PythonFunction);
		}
		return 0;

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
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
			for (auto key : _keys) {
				if (strcmp(key.Name.c_str(), (const char*)cds->lpData) == 0) {
					stringstream ss;
					ss << "Macro '" << key.Name << "' called\r\n";
					LogMessage(ss.str());
					WaitForKeyboard();
					run_fun(key.PythonFunction);
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
		SendDlgItemMessage(hDlg, IDC_VERSION, WM_SETTEXT, 0, (LPARAM)_T("Version ") VERSION_TEXT_T _T(", Copyright \xA9 ") VERSION_YEAR_T);
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
			DoEvents();
		}
		else {
			break;
		}
	}
}

void LoadPython() {
	for (auto key : _keys) {
		if (key.VK != 0) {
			UnregisterHotKey(_hWndMain, key.ID);
		}
	}
	_keys.clear();
	// TODO: Need to clean up memory from any previous runs.

	HANDLE hFile = CreateFile(
#ifdef _DEBUG
		_T("macro\\MicroKeys.py"), 
#else
		_T("MicroKeys.py"),
#endif
		GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		LogMessage("Unable to open MicroKeys.py\r\n");
		return;
	}

	DWORD len = GetFileSize(hFile, NULL);
	char* data = (char*)malloc((size_t)len + 1);
	if (data == NULL) {
		LogMessage("Failure reading data\r\n");
		return;
	}

	memset(data, 0, (size_t)len + 1);
	if (!ReadFile(hFile, data, len, NULL, NULL)) {
		LogMessage("Unable to read data\r\n");
		return;
	}

	int ret = run_micro_python(data);
	free(data);
	if (ret != 0) {
		stringstream ss;
		ss << "Call returned: " << ret << "\r\n";
		LogMessage(ss.str());
	}
}
