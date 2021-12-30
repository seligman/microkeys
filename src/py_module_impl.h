#pragma once

extern "C" {
	int run_micro_python(const char* code);
	void keys_press_invoke(const char* msg);
	int run_micro_python(const char* code);
	int run_fun(void* fun);
	char* clip_copy_invoke();
	void clip_paste_invoke(const char*);
}
