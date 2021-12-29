#pragma once

#include "resource.h"

typedef struct {
	char desc[50];
	char name[250];
	int vk;
	void* fun;
} key_struct;

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
void LogMessage(const char* msg);
void LoadPython();
void WaitForKeyboard();
HWND GetMainWindow();
void GetKeysInfo(int** pNumKeys, key_struct** keys);

#define MAX_KEYS 100
#define MAX_LOADSTRING 100
#define MICROKEYS_MUTEX _T("MicroKeys_nZ3SC4MxQsEfQlmmWhc2WaELCZrGdam1IGrOh7XL")
#define CDS_INVOKE_MACRO	1001
