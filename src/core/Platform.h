#pragma once

#include "Core.h"

// The platform interface.  Everything above this line (the core, the
// _microkeys module, and the Python package) only talks to the OS through
// these classes.  Each OS supplies one implementation.

struct WindowInfo {
	uint64_t Handle = 0;
	string Title;
	string ClassName;
	string Process;
	uint32_t Pid = 0;
};

struct ScreenRect {
	int X = 0;
	int Y = 0;
	int Width = 0;
	int Height = 0;
};

struct MonitorInfo {
	ScreenRect Bounds;
	ScreenRect Work;
	bool Primary = false;
	int Dpi = 96;
};

class WindowFilter {
	// Decides if a hotkey or hotstring applies to the foreground window.
	// Each field is a case insensitive glob; empty fields match anything.
public:
	string Process;
	string Title;
	string ClassName;

	bool IsEmpty() const;
	bool Matches(const WindowInfo& window) const;
	string Describe() const;
};

class HotkeyBinding {
public:
	int ID = 0;
	int Mods = 0;
	string Key;
	WindowFilter When;
	string Description;
};

class HotstringBinding {
public:
	int ID = 0;
	u32string Trigger;
	bool Immediate = true;
	bool CaseSensitive = false;
	WindowFilter When;
};

class CommandBinding {
public:
	int ID = 0;
	string Name;
};

class WindowEventBinding {
	// A window coming to the front, or appearing
public:
	int ID = 0;
	int Event = WINDOW_ACTIVATE;
	WindowFilter When;
};

class Bindings {
public:
	vector<HotkeyBinding> Hotkeys;
	vector<HotstringBinding> Hotstrings;
	vector<CommandBinding> Commands;
	vector<WindowEventBinding> WindowEvents;
};

class KeyAction {
	// One step of keyboard output: a named key going down or up, or a
	// character typed as text regardless of the keyboard layout
public:
	enum Kind { Down, Up, Char };
	Kind Type = Char;
	string Key;
	char32_t Character = 0;
};

class ClipItem {
	// One kind of clipboard data.  Kind is "text", "html" (the copied
	// fragment), "rtf", "files" (paths joined with NUL), or "image" (PNG),
	// all in UTF-8 where it's text.  A saved clipboard uses the platform's
	// own format names instead.
public:
	string Kind;
	string Data;
};

class IHotkeys {
	// Global hotkey registration, always called on the UI thread
public:
	virtual ~IHotkeys() {}
	virtual bool RegisterHotkey(int id, int mods, const string& key, string& error) = 0;
	virtual void UnregisterHotkey(int id) = 0;
};

class IPlatform {
	// The services the script thread uses.  Methods may be called from the
	// script thread at any time; implementations marshal to their UI thread
	// where the OS requires it.
public:
	virtual ~IPlatform() {}

	// Keyboard
	virtual bool IsKeyName(const string& name) = 0;
	virtual bool SendKeys(const vector<KeyAction>& actions, int delayMs, const atomic<bool>& cancel, string& error) = 0;
	virtual bool ModifiersHeld() = 0;

	// Mouse
	virtual void GetCursor(int& x, int& y) = 0;
	virtual void MoveCursor(int x, int y) = 0;
	virtual bool MouseButton(const string& button, bool down, string& error) = 0;
	virtual void MouseWheel(int clicks, bool horizontal) = 0;

	// Clipboard
	virtual bool GetClipboardText(string& text) = 0;
	virtual bool SetClipboardText(const string& text) = 0;
	virtual uint32_t ClipboardSequence() = 0;
	virtual vector<string> ClipboardKinds() = 0;
	virtual bool ReadClipboard(const string& kind, string& data) = 0;
	virtual bool WriteClipboard(const vector<ClipItem>& items, string& error) = 0;
	virtual bool SaveClipboard(vector<ClipItem>& items) = 0;
	virtual bool RestoreClipboard(const vector<ClipItem>& items) = 0;
	virtual bool ClipboardConcealed() = 0;

	// Windows
	virtual vector<WindowInfo> ListWindows(bool includeHidden) = 0;
	virtual WindowInfo ForegroundWindow() = 0;
	virtual bool GetWindowDetails(uint64_t handle, WindowInfo& info) = 0;
	virtual bool ActivateWindow(uint64_t handle) = 0;
	virtual bool GetWindowBounds(uint64_t handle, ScreenRect& rect) = 0;
	virtual bool SetWindowBounds(uint64_t handle, const ScreenRect& rect) = 0;
	virtual string GetWindowState(uint64_t handle) = 0;
	virtual bool SetWindowState(uint64_t handle, const string& state) = 0;
	virtual bool RequestClose(uint64_t handle) = 0;
	virtual vector<MonitorInfo> Monitors() = 0;
	virtual int WindowMonitor(uint64_t handle) = 0;

	// User interface
	virtual string ShowMessage(const string& text, const string& title, const string& buttons) = 0;
	virtual bool AskText(const string& prompt, const string& title, const string& initial, string& result) = 0;
	virtual int AskChoice(const vector<string>& items, const string& prompt, const string& title) = 0;
	virtual void Toast(const string& title, const string& text) = 0;
	// Empty text takes it down; a negative timeout leaves it up
	virtual void ShowOverlay(const string& text, int timeoutMs) = 0;

	// System
	virtual bool ShellOpen(const string& target, string& error) = 0;
	virtual string PlatformName() = 0;
};

class ShellConfig {
	// Settings the scripts control that the app shell acts on
public:
	string ScriptDir;
	int AbortMods = 0;
	string AbortKey;
	string UpdateFeed;
	bool CheckUpdates = true;
	string Editor;
};

class IAppShell {
	// The app around the script engine: the log window, the trigger
	// registrations, and the settings the scripts control.  Callable from any
	// thread.
public:
	virtual ~IAppShell() {}
	virtual void Log(int level, const string& text) = 0;
	virtual void SetBindings(const Bindings& bindings) = 0;
	virtual void Configure(const ShellConfig& config) = 0;
	virtual void ScriptsLoaded(bool ok, const string& summary) = 0;
	virtual void MacroState(const string& running) = 0;
	virtual void ConsoleState(bool more) = 0;
};
