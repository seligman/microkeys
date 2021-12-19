#include "framework.h"
#include "MicroKeys.h"

#define MAX_LOADSTRING 100

extern "C" {
    int run_micro_python(const char* code);
    void keys_press_invoke(const char* msg);
    int run_micro_python(const char* code);
    int run_fun(void* fun);
}

HINSTANCE _hInst;
WCHAR _szTitle[MAX_LOADSTRING];
WCHAR _szWindowClass[MAX_LOADSTRING];

HWND _hWndMain;
HWND _hWndTest;
HWND _hWndEdit;

int _numKeys = 0;
int _curKey = 0;
int _runKey = -1;

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
void Resize(HWND hWnd);
void LogMessage(const char* msg);
void LoadPython();

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    LoadStringW(hInstance, IDS_APP_TITLE, _szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_MICROKEYS, _szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance (hInstance, nCmdShow)) {
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

    return (int) msg.wParam;
}

//extern "C" int main() {
//    return wWinMain(GetModuleHandle(NULL), NULL, NULL, SW_SHOW);
//}

ATOM MyRegisterClass(HINSTANCE hInstance){
    WNDCLASSEXW wcex = { 0 };

    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MICROKEYS));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_MICROKEYS);
    wcex.lpszClassName = _szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
   _hInst = hInstance;

   _hWndMain = CreateWindowW(
       _szWindowClass, _szTitle, WS_OVERLAPPEDWINDOW,
       CW_USEDEFAULT, CW_USEDEFAULT, 
       500, 500, 
       nullptr, nullptr, hInstance, nullptr);

   if (!_hWndMain) {
      return FALSE;
   }

   _hWndTest = CreateWindowEx(WS_EX_CLIENTEDGE, _T("Button"), _T("Test"),
       WS_CHILD | WS_VISIBLE, 0, 0, 20, 60, _hWndMain, NULL, NULL, NULL);
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

   return TRUE;
}

void LogMessage(const char* msg) {
    int index = GetWindowTextLength(_hWndEdit);
    // Escape hatch for the input box getting too big
    if (SendMessage(_hWndEdit, WM_GETTEXTLENGTH, 0, 0) > 10 * 1024 * 1024) {
        SendMessage(_hWndEdit, WM_SETTEXT, (WPARAM)0, (LPARAM)_T(""));
    }
    SendMessage(_hWndEdit, EM_SETSEL, (WPARAM)index, (LPARAM)index);
    SendMessageA(_hWndEdit, EM_REPLACESEL, 0, (LPARAM)msg);

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


#define START_HEIGHT 25
#define START_WIDTH 60
#define START_VERT_MARGIN 5
#define START_HORZ_MARGIN 5

void Resize(HWND hWnd) {
    RECT rt;
    GetClientRect(hWnd, &rt);
    MoveWindow(_hWndTest, START_HORZ_MARGIN + rt.left, rt.top + START_VERT_MARGIN, START_WIDTH, START_HEIGHT, TRUE);
    int topDist = START_HEIGHT + (START_VERT_MARGIN * 2);
    MoveWindow(_hWndEdit, rt.left, rt.top + topDist, rt.right - rt.left, rt.bottom - rt.top - topDist, TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_HOTKEY:
        if ((int)wParam <= _numKeys && wParam > 0) {
            _runKey = ((int)wParam) - 1;
            _curKey = 0;
            LoadPython();
        }
        return 0;

    case WM_COMMAND:
        {
            int cmd = HIWORD(wParam);
            switch (cmd) {
            case BN_CLICKED:
                if ((HWND)lParam == _hWndTest) {
                    _numKeys = 0;
                    _runKey = -1;
                    _curKey = 0;
                    LoadPython();
                }
                break;
            }

            int wmId = LOWORD(wParam);
            switch (wmId)
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

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
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

extern "C" void keys_press_invoke(const char* msg) {
    char temp[1000];
    sprintf_s(temp, "%s\r\n", msg);
    LogMessage(temp);
}

extern "C" void key_press_store_fun(int vk, void* fun) {
    if (_runKey < 0) {
        _numKeys++;
        RegisterHotKey(_hWndMain, _numKeys, (vk & 0xff00) >> 8, vk & 0xff);
    } else {
        if (_curKey == _runKey) {
            run_fun(fun);
        }
        _curKey++;
    }
}

void LoadPython() {
    HANDLE hFile = CreateFile(_T("MicroKeys.py"), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogMessage("Unable to open MicroKeys.py\r\n");
        return;
    }

    DWORD len = GetFileSize(hFile, NULL);
    char* data = (char*)malloc(len + 1);
    if (data == NULL) {
        LogMessage("Failure reading data\r\n");
        return;
    }

    memset(data, 0, len + 1);
    if (!ReadFile(hFile, data, len, NULL, NULL)) {
        LogMessage("Unable to read data\r\n");
        return;
    }

    int ret = run_micro_python(data);
    free(data);
    char msg[100];
    sprintf_s(msg, "Call returned: %d\r\n", ret);
    LogMessage(msg);
}
