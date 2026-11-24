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

class RemapBinding {
	// One key acting as another, or (with no target) doing nothing
public:
	int ID = 0;
	string Source;
	string Target;
	WindowFilter When;
};

class Bindings {
public:
	vector<HotkeyBinding> Hotkeys;
	vector<HotstringBinding> Hotstrings;
	vector<CommandBinding> Commands;
	vector<WindowEventBinding> WindowEvents;
	vector<RemapBinding> Remaps;
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

class Bitmap {
	// 32-bit pixels, top row first, each one blue, green, red, alpha
public:
	int Width = 0;
	int Height = 0;
	string Pixels;
};

class ControlQuery {
	// What to look for among a window's controls.  Name and ClassName are
	// case insensitive globs, Role is one of the names RoleName gives, and
	// empty fields match anything.
public:
	string Name;
	string Role;
	string AutomationId;
	string ClassName;
};

class ControlInfo {
	// One control's details.  Toggle, Selected and Expanded are -1 when the
	// control has no such state.
public:
	string Name;
	string Role;
	string AutomationId;
	string ClassName;
	string RuntimeId;
	bool Enabled = false;
	bool Focused = false;
	bool Offscreen = false;
	ScreenRect Bounds;
	uint64_t Window = 0;
	bool HasValue = false;
	bool ReadOnly = true;
	string Value;
	int Toggle = -1;
	int Selected = -1;
	int Expanded = -1;
	vector<string> Actions;
};

class TextWord {
	// A word read off an image (OCR): where it is in the image, and which
	// line it's on, counting from 0 in reading order
public:
	string Text;
	ScreenRect Bounds;
	int Line = 0;
};

class TextMatch {
	// Text found through accessibility.  A match in a document has a
	// rectangle per line it covers, its own text, and a few characters
	// either side of it, for telling whole words from parts of words.  A
	// control matched by its name (or value, for a text box) has the
	// control's rectangle and that whole name or value.
public:
	vector<ScreenRect> Rects;
	bool InDocument = false;
	string Text;
	string Context;
	string Name;
};

class FileType {
	// A choice in a file dialog's type list, like "Zip files" and "*.zip"
public:
	string Name;
	string Pattern;
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
	virtual bool SendKeys(const vector<KeyAction>& actions, int delayMs, int holdMs, int frameMs, const atomic<bool>& cancel, string& error) = 0;
	virtual bool ModifiersHeld() = 0;
	virtual bool UsesDirectInput() = 0;

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
	virtual bool IsElevatedWindow(uint64_t handle) = 0;

	// Screen and images
	virtual bool CaptureScreen(const ScreenRect& rect, Bitmap& bitmap, string& error) = 0;
	virtual bool DecodeImage(const string& data, Bitmap& bitmap, string& error) = 0;
	virtual bool EncodePng(const Bitmap& bitmap, string& png, string& error) = 0;
	// Reading text off an image with the system's OCR.  An empty language
	// means the user's own.  The image is scaled up by scale first, for
	// small text, where there's room.
	virtual bool ReadText(const Bitmap& bitmap, const string& language, int scale, vector<TextWord>& words, string& error) = 0;
	virtual vector<string> TextLanguages() = 0;

	// Controls inside windows (UI Automation, the Accessibility API).  A
	// control is a handle that stays valid until it's released.
	virtual bool FindControls(uint64_t root, uint64_t window, const ControlQuery& query, bool all, vector<uint64_t>& found, string& error) = 0;
	virtual bool GetControlInfo(uint64_t control, ControlInfo& info) = 0;
	virtual bool GetControlText(uint64_t control, string& text) = 0;
	virtual bool ControlAction(uint64_t control, const string& action, const string& value, string& error) = 0;
	virtual vector<uint64_t> RelatedControls(uint64_t control, bool parent) = 0;
	virtual uint64_t FocusedControl() = 0;
	virtual uint64_t ControlAt(int x, int y) = 0;
	virtual void ReleaseControl(uint64_t control) = 0;
	// Where text shows on screen, searching the text of documents (and text
	// boxes), or else controls' names, under root or the window (0 for the
	// one in front).  Only text that can be seen, not scrolled away or
	// covered, is found.
	virtual bool FindControlText(uint64_t root, uint64_t window, const string& text, bool matchCase, bool documents, size_t limit,
		vector<TextMatch>& found, string& error) = 0;

	// User interface
	virtual string ShowMessage(const string& text, const string& title, const string& buttons) = 0;
	virtual bool AskText(const string& prompt, const string& title, const string& initial, string& result) = 0;
	virtual int AskChoice(const vector<string>& items, const string& prompt, const string& title) = 0;
	virtual void Toast(const string& title, const string& text) = 0;
	// Empty text takes it down; a negative timeout leaves it up
	virtual void ShowOverlay(const string& text, int timeoutMs) = 0;
	virtual bool AskFile(bool save, const string& title, const vector<FileType>& types, const string& initial, string& path) = 0;

	// Finding other MicroKeys machines on the local network (Bonjour).  An
	// empty name stops advertising.
	virtual bool Advertise(const string& name, int port, string& error) = 0;
	virtual bool Resolve(const string& name, int timeoutMs, string& host, int& port) = 0;
	virtual vector<string> Browse(int timeoutMs) = 0;

	// System
	virtual bool ShellOpen(const string& target, string& error) = 0;
	virtual string PlatformName() = 0;
	virtual bool IsElevated() = 0;
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
	bool RunElevated = false;
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
