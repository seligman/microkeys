#pragma once

#include "framework.h"

// Tags the input MicroKeys sends, so its own typing never triggers a hotstring
#define MICROKEYS_INPUT_TAG	((ULONG_PTR)0x4D4B4559)

class WinPlatform : public IPlatform {
	// The Windows implementation of the platform services scripts use
public:
	HWND ClipboardOwner = NULL;
	function<void(const string&, const string&)> OnToast;

	bool IsKeyName(const string& name) override;
	bool SendKeys(const vector<KeyAction>& actions, int delayMs, const atomic<bool>& cancel, string& error) override;
	bool ModifiersHeld() override;

	void GetCursor(int& x, int& y) override;
	void MoveCursor(int x, int y) override;
	bool MouseButton(const string& button, bool down, string& error) override;
	void MouseWheel(int clicks, bool horizontal) override;

	bool GetClipboardText(string& text) override;
	bool SetClipboardText(const string& text) override;
	uint32_t ClipboardSequence() override;

	vector<WindowInfo> ListWindows(bool includeHidden) override;
	WindowInfo ForegroundWindow() override;
	bool GetWindowDetails(uint64_t handle, WindowInfo& info) override;
	bool ActivateWindow(uint64_t handle) override;
	bool GetWindowBounds(uint64_t handle, ScreenRect& rect) override;
	bool SetWindowBounds(uint64_t handle, const ScreenRect& rect) override;
	string GetWindowState(uint64_t handle) override;
	bool SetWindowState(uint64_t handle, const string& state) override;
	bool RequestClose(uint64_t handle) override;
	vector<MonitorInfo> Monitors() override;
	int WindowMonitor(uint64_t handle) override;

	string ShowMessage(const string& text, const string& title, const string& buttons) override;
	bool AskText(const string& prompt, const string& title, const string& initial, string& result) override;
	int AskChoice(const vector<string>& items, const string& prompt, const string& title) override;
	void Toast(const string& title, const string& text) override;

	bool ShellOpen(const string& target, string& error) override;
	string PlatformName() override;
};

WindowInfo DescribeWindow(HWND hwnd);
