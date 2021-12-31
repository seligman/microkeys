#pragma once

extern "C" {
	int run_micro_python(const char* code);
	void keys_press_invoke(const char* msg);
	void keys_press_raw_invoke(const char* msg);
	int run_micro_python(const char* code);
	int run_fun(void* fun);
	char* clip_copy_invoke();
	void clip_paste_invoke(const char*);
	void mouse_position_invoke(int* x, int* y);
	void mouse_move_invoke(int x, int y, int offset);
	void mouse_click_invoke(int left_down, int left_up, int right_down, int right_up);
}
