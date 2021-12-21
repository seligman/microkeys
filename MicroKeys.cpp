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
HWND _hWndEdit;

typedef struct {
    char desc[50];
    int vk;
    void* fun;
} key_struct;
#define MAX_KEYS 100
key_struct _keys[MAX_KEYS];
int _numKeys = 0;
bool _handleHotkeys = true;

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void DoEvents();
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
void Resize(HWND hWnd);
void LogMessage(const char* msg);
void LoadPython();
void WaitForKeyboard();

void DoEvents() {
    _handleHotkeys = false;
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
        if (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            break;
        }
    }
    _handleHotkeys = true;
}

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
        if (_handleHotkeys && (int)wParam <= _numKeys && wParam > 0) {
            char temp[1000];
            sprintf_s(temp, "Keypress of %s detected\r\n", _keys[wParam - 1].desc);
            LogMessage(temp);
            WaitForKeyboard();
            run_fun(_keys[wParam - 1].fun);
        }
        return 0;

    case WM_COMMAND:
        {
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
        } else {
            break;
        }
    }
}

extern "C" void keys_press_invoke(const char* msg) {
    char temp[1000];
    sprintf_s(temp, "keys.press('%s')\r\n", msg);
    LogMessage(temp);

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

void AddDesc(char* dest, const char* val) {
    if (strlen(dest) > 0) {
        strcat_s(dest, 50, "+");
    }
    strcat_s(dest, 50, val);
}

extern "C" void key_press_store_fun(int vk, void* fun) {
    if (_numKeys >= MAX_KEYS) {
        return;
    }

    _numKeys++;
    RegisterHotKey(_hWndMain, _numKeys, (vk & 0xff00) >> 8, vk & 0xff);
    _keys[_numKeys - 1].vk = vk;
    _keys[_numKeys - 1].fun = fun;

    char desc[50] = { 0 };
    char temp[1000] = { 0 };

    if ((vk & 0x100) > 0) { AddDesc(desc, "Alt"); }
    if ((vk & 0x200) > 0) { AddDesc(desc, "Control"); }
    if ((vk & 0x400) > 0) { AddDesc(desc, "Shift"); }
    if ((vk & 0x800) > 0) { AddDesc(desc, "Windows"); }

    switch (vk & 0xFF) {
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
    case 0x08: AddDesc(desc, "Back"); break;
    case 0x43: AddDesc(desc, "C"); break;
    case 0x0C: AddDesc(desc, "Clear"); break;
    case 0x11: AddDesc(desc, "Control"); break;
    case 0x44: AddDesc(desc, "D"); break;
    case 0x2E: AddDesc(desc, "Delete"); break;
    case 0x28: AddDesc(desc, "Down"); break;
    case 0x45: AddDesc(desc, "E"); break;
    case 0x23: AddDesc(desc, "End"); break;
    case 0x0D: AddDesc(desc, "Enter"); break;
    case 0x1B: AddDesc(desc, "Escape"); break;
    case 0x46: AddDesc(desc, "F"); break;
    case 0x70: AddDesc(desc, "F1"); break;
    case 0x79: AddDesc(desc, "F10"); break;
    case 0x7A: AddDesc(desc, "F11"); break;
    case 0x7B: AddDesc(desc, "F12"); break;
    case 0x71: AddDesc(desc, "F2"); break;
    case 0x72: AddDesc(desc, "F3"); break;
    case 0x73: AddDesc(desc, "F4"); break;
    case 0x74: AddDesc(desc, "F5"); break;
    case 0x75: AddDesc(desc, "F6"); break;
    case 0x76: AddDesc(desc, "F7"); break;
    case 0x77: AddDesc(desc, "F8"); break;
    case 0x78: AddDesc(desc, "F9"); break;
    case 0x47: AddDesc(desc, "G"); break;
    case 0x48: AddDesc(desc, "H"); break;
    case 0x24: AddDesc(desc, "Home"); break;
    case 0x49: AddDesc(desc, "I"); break;
    case 0x2D: AddDesc(desc, "Insert"); break;
    case 0x4A: AddDesc(desc, "J"); break;
    case 0x4B: AddDesc(desc, "K"); break;
    case 0x4C: AddDesc(desc, "L"); break;
    case 0x25: AddDesc(desc, "Left"); break;
    case 0x4D: AddDesc(desc, "M"); break;
    case 0x4E: AddDesc(desc, "N"); break;
    case 0x4F: AddDesc(desc, "O"); break;
    case 0x50: AddDesc(desc, "P"); break;
    case 0x51: AddDesc(desc, "Q"); break;
    case 0x52: AddDesc(desc, "R"); break;
    case 0x27: AddDesc(desc, "Right"); break;
    case 0x53: AddDesc(desc, "S"); break;
    case 0x20: AddDesc(desc, "Space"); break;
    case 0x54: AddDesc(desc, "T"); break;
    case 0x09: AddDesc(desc, "Tab"); break;
    case 0x55: AddDesc(desc, "U"); break;
    case 0x26: AddDesc(desc, "Up"); break;
    case 0x56: AddDesc(desc, "V"); break;
    case 0x57: AddDesc(desc, "W"); break;
    case 0x58: AddDesc(desc, "X"); break;
    case 0x59: AddDesc(desc, "Y"); break;
    case 0x5A: AddDesc(desc, "Z"); break;
    default: AddDesc(desc, "<Unknown>"); break;
    }

    strcat_s(_keys[_numKeys - 1].desc, desc);
    sprintf_s(temp, "Registered %s\r\n", desc);
    LogMessage(temp);
}

void LoadPython() {
    for (int i = 0; i < _numKeys; i++) {
        UnregisterHotKey(_hWndMain, i + 1);
    }
    _numKeys = 0;
    // TODO: Need to clean up memory from any previous runs.

    HANDLE hFile = CreateFile(_T("MicroKeys.py"), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
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
    char msg[100];
    sprintf_s(msg, "Call returned: %d\r\n", ret);
    LogMessage(msg);
}
