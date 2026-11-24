#pragma once

#include "framework.h"

// Tags the input MicroKeys sends, so its own typing never triggers a hotstring
#define MICROKEYS_INPUT_TAG	((ULONG_PTR)0x4D4B4559)
// Tags the keys a remapped key sends, which count as typing but aren't
// remapped again
#define MICROKEYS_REMAP_TAG	((ULONG_PTR)0x4D4B5250)

class WinPlatform : public IPlatform {
	// The Windows implementation of the platform services scripts use
public:
	HWND ClipboardOwner = NULL;
	function<void(const string&, const string&)> OnToast;
	function<void(const string&, int)> OnOverlay;

	bool IsKeyName(const string& name) override;
	bool SendKeys(const vector<KeyAction>& actions, int delayMs, int holdMs, int frameMs, const atomic<bool>& cancel, string& error) override;
	// For the keyboard hook, which has to answer quickly: everything goes out
	// at once, without waiting on the program in front
	bool SendKeysNow(const vector<KeyAction>& actions, string& error);
	bool ModifiersHeld() override;
	bool UsesDirectInput() override;

	void GetCursor(int& x, int& y) override;
	void MoveCursor(int x, int y) override;
	bool MouseButton(const string& button, bool down, string& error) override;
	void MouseWheel(int clicks, bool horizontal) override;

	bool GetClipboardText(string& text) override;
	bool SetClipboardText(const string& text) override;
	uint32_t ClipboardSequence() override;
	vector<string> ClipboardKinds() override;
	bool ReadClipboard(const string& kind, string& data) override;
	bool WriteClipboard(const vector<ClipItem>& items, string& error) override;
	bool SaveClipboard(vector<ClipItem>& items) override;
	bool RestoreClipboard(const vector<ClipItem>& items) override;
	bool ClipboardConcealed() override;

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
	bool IsElevatedWindow(uint64_t handle) override;

	bool CaptureScreen(const ScreenRect& rect, Bitmap& bitmap, string& error) override;
	bool DecodeImage(const string& data, Bitmap& bitmap, string& error) override;
	bool EncodePng(const Bitmap& bitmap, string& png, string& error) override;
	bool ReadText(const Bitmap& bitmap, const string& language, int scale, vector<TextWord>& words, string& error) override;
	vector<string> TextLanguages() override;

	bool FindControls(uint64_t root, uint64_t window, const ControlQuery& query, bool all, vector<uint64_t>& found, string& error) override;
	bool GetControlInfo(uint64_t control, ControlInfo& info) override;
	bool GetControlText(uint64_t control, string& text) override;
	bool ControlAction(uint64_t control, const string& action, const string& value, string& error) override;
	vector<uint64_t> RelatedControls(uint64_t control, bool parent) override;
	uint64_t FocusedControl() override;
	uint64_t ControlAt(int x, int y) override;
	void ReleaseControl(uint64_t control) override;
	bool FindControlText(uint64_t root, uint64_t window, const string& text, bool matchCase, bool documents, size_t limit,
		vector<TextMatch>& found, string& error) override;

	string ShowMessage(const string& text, const string& title, const string& buttons) override;
	bool AskText(const string& prompt, const string& title, const string& initial, string& result) override;
	int AskChoice(const vector<string>& items, const string& prompt, const string& title) override;
	void Toast(const string& title, const string& text) override;
	void ShowOverlay(const string& text, int timeoutMs) override;
	bool AskFile(bool save, const string& title, const vector<FileType>& types, const string& initial, string& path) override;

	bool Advertise(const string& name, int port, string& error) override;
	bool Resolve(const string& name, int timeoutMs, string& host, int& port) override;
	vector<string> Browse(int timeoutMs) override;

	bool ShellOpen(const string& target, string& error) override;
	string PlatformName() override;
	bool IsElevated() override;
};

WindowInfo DescribeWindow(HWND hwnd);
// Images as PNG (and from anything WIC reads), for the clipboard and the screen
bool PixelsToPng(const vector<BYTE>& bgra, int width, int height, string& png);
bool ImageToPixels(const string& data, vector<BYTE>& bgra, int& width, int& height);
