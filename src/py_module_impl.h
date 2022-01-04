#pragma once

class ModState;

void press_key(char key, SHORT vk, ModState& shiftState, ModState& altState, ModState& ctrlState);
void AddDesc(stringstream& desc, string val);
void mouse_helper(DWORD event, int vk, int check);
string GetWindowText(HWND hwnd);
string GetClassName(HWND hwnd);
string HandleToStr(HWND hwnd);
HWND StrToHandle(string hwnd);
BOOL CALLBACK EnumWindowsProc(_In_ HWND hwnd, _In_ LPARAM lParam);

extern "C" {
	int run_micro_python(const char* code);
	int run_fun(void* fun);

	void keys_log_invoke(const char* msg);
	void keys_press_invoke(const char* msg);
	void keys_press_raw_invoke(const char* msg);

	char* clip_copy_invoke();
	void clip_paste_invoke(const char*);

	void mouse_position_invoke(int* x, int* y);
	void mouse_move_invoke(int x, int y, int offset);
	void mouse_click_invoke(int left_down, int left_up, int right_down, int right_up);

	void* windows_get_active_impl();
	void windows_list_all_impl(void*** list, int* count);
	void windows_set_active_impl(const char* handle);
	void windows_get_position_impl(const char* handle, int* x, int* y, int* width, int* height);
	void windows_set_position_impl(const char* handle, int x, int y, int width, int height);
	void windows_launch_impl(const char* command_line);
}
