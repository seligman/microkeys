#pragma once

#include "Platform.h"

class ITriggerSink {
	// Where the trigger registry sends its decisions.  The app shell
	// implements this on the UI thread.
public:
	virtual ~ITriggerSink() {}
	virtual bool RegisterHotkey(int id, int mods, const string& key, string& error) = 0;
	virtual void UnregisterHotkey(int id) = 0;
	virtual void Fire(int kind, int id, const string& extra) = 0;
	virtual void TriggerLog(int level, const string& text) = 0;
};

class Triggers {
	// Owns the current hotkeys, hotstrings, and commands, and decides which
	// hotkeys should be registered for the foreground window.  Runs on the UI
	// thread and never waits on Python.
public:
	Triggers(ITriggerSink* sink);
	void SetBindings(const Bindings& bindings, const WindowInfo& foreground);
	void Clear();
	void SetPaused(bool paused, const WindowInfo& foreground);
	bool IsPaused() const;
	void OnForeground(const WindowInfo& foreground);
	void OnTitleChange(const WindowInfo& foreground);
	bool OnHotkey(int id);
	void OnChar(char32_t c, const WindowInfo& foreground);
	void OnBackspace();
	void ResetBuffer();
	int FindCommand(const string& name) const;
	bool HasHotstrings() const;
	bool WatchesTitles() const;
	bool HasFilters() const;
	set<int> Registered() const;
	string HotkeyDescription(int id) const;

private:
	void Refresh(const WindowInfo& foreground);
	bool MatchHotstring(const HotstringBinding& hs, char32_t endChar, const WindowInfo& foreground) const;

	ITriggerSink* _sink;
	Bindings _bindings;
	set<int> _registered;
	set<int> _reported;
	bool _paused = false;
	u32string _buffer;
};
