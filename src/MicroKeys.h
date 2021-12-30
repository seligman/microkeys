#pragma once

#include "resource.h"

using namespace std;

class KeyData {
public:
	int ID;
	string Description;
	string Name;
	int VK;
	void* PythonFunction;
};

extern "C" {
	int run_micro_python(const char* code);
	void keys_press_invoke(const char* msg);
	int run_micro_python(const char* code);
	int run_fun(void* fun);
	char* clip_copy_invoke();
	void clip_paste_invoke(const char*);
}
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void DoEvents();
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
void Resize(HWND hWnd);
void LogMessage(string msg);
void LoadPython();
void WaitForKeyboard();
HWND GetMainWindow();
vector<KeyData>& GetKeys();

#define MICROKEYS_MUTEX L"MicroKeys_nZ3SC4MxQsEfQlmmWhc2WaELCZrGdam1IGrOh7XL"
#define CDS_INVOKE_MACRO	1001
