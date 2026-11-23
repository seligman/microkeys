#include "Triggers.h"
#include "Text.h"

#define HOTSTRING_BUFFER	100
// How long after a window appears a title change can still make it match
#define WINDOW_TITLE_WAIT	2000

static const u32string _endChars = U" \n\t.,;:!?-()[]{}\"'/\\";

bool WindowFilter::IsEmpty() const {
	// True when this filter applies everywhere
	return Process.empty() && Title.empty() && ClassName.empty();
}

bool WindowFilter::Matches(const WindowInfo& window) const {
	// Every field that's set must match
	if (!Process.empty() && !GlobMatch(Process, window.Process)) {
		return false;
	}
	if (!Title.empty() && !GlobMatch(Title, window.Title)) {
		return false;
	}
	if (!ClassName.empty() && !GlobMatch(ClassName, window.ClassName)) {
		return false;
	}
	return true;
}

string WindowFilter::Describe() const {
	// Short text for the log, like "process=notepad.exe"
	string ret;
	if (!Process.empty()) {
		ret += "process=" + Process;
	}
	if (!Title.empty()) {
		ret += (ret.empty() ? "" : ", ") + string("title=") + Title;
	}
	if (!ClassName.empty()) {
		ret += (ret.empty() ? "" : ", ") + string("class=") + ClassName;
	}
	return ret;
}

Triggers::Triggers(ITriggerSink* sink) {
	_sink = sink;
}

void Triggers::SetBindings(const Bindings& bindings, const WindowInfo& foreground) {
	// Swap in a new set of bindings, releasing everything the old set held.
	// A binding keeps its ID while it exists, so a problem with one that's
	// already been reported isn't reported again, and a new window still
	// waiting for its title keeps waiting.
	map<uint64_t, PendingWindow> pending = _pending;
	Clear();
	_bindings = bindings;
	for (auto& cur : pending) {
		vector<int> ids;
		for (int id : cur.second.IDs) {
			for (auto& ev : _bindings.WindowEvents) {
				if (ev.ID == id && ev.Event == WINDOW_OPEN) {
					ids.push_back(id);
				}
			}
		}
		if (!ids.empty()) {
			cur.second.IDs = ids;
			_pending[cur.first] = cur.second;
		}
	}
	set<int> reported;
	for (auto& hk : _bindings.Hotkeys) {
		if (_reported.count(hk.ID) > 0) {
			reported.insert(hk.ID);
		}
	}
	_reported = reported;
	_lastActive = foreground.Handle;
	Refresh(foreground);
}

void Triggers::Clear() {
	// Unregister every hotkey we hold
	for (int id : _registered) {
		_sink->UnregisterHotkey(id);
	}
	_registered.clear();
	_bindings = Bindings();
	_buffer.clear();
	_pending.clear();
}

void Triggers::SetPaused(bool paused, const WindowInfo& foreground) {
	// While paused nothing is registered and hotstrings are ignored
	_paused = paused;
	_buffer.clear();
	Refresh(foreground);
}

bool Triggers::IsPaused() const {
	return _paused;
}

void Triggers::OnForeground(const WindowInfo& foreground) {
	// The foreground window changed (or its title did), so per-app hotkeys
	// may need to come or go.  Typing in one window doesn't carry to another.
	_buffer.clear();
	Refresh(foreground);
}

void Triggers::OnTitleChange(const WindowInfo& foreground) {
	// Same window, new title (editors add a "*" as soon as you type), so
	// re-check the filters but keep what's been typed
	Refresh(foreground);
}

void Triggers::Refresh(const WindowInfo& foreground) {
	// For each key combination pick the binding that applies right now: the
	// first one whose filter matches, else the first one without a filter
	map<pair<int, string>, const HotkeyBinding*> chosen;
	if (!_paused) {
		for (auto& hk : _bindings.Hotkeys) {
			auto combo = make_pair(hk.Mods, hk.Key);
			auto existing = chosen.find(combo);
			if (hk.When.IsEmpty()) {
				if (existing == chosen.end()) {
					chosen[combo] = &hk;
				}
			}
			else if (hk.When.Matches(foreground)) {
				if (existing == chosen.end() || existing->second->When.IsEmpty()) {
					chosen[combo] = &hk;
				}
			}
		}
	}

	set<int> wanted;
	for (auto& cur : chosen) {
		wanted.insert(cur.second->ID);
	}

	// Release first, since the same key may move to a different ID
	for (auto it = _registered.begin(); it != _registered.end();) {
		if (wanted.count(*it) == 0) {
			_sink->UnregisterHotkey(*it);
			it = _registered.erase(it);
		}
		else {
			++it;
		}
	}

	for (auto& cur : chosen) {
		const HotkeyBinding* hk = cur.second;
		if (_registered.count(hk->ID) > 0) {
			continue;
		}
		string error;
		if (_sink->RegisterHotkey(hk->ID, hk->Mods, hk->Key, error)) {
			_registered.insert(hk->ID);
		}
		else if (_reported.count(hk->ID) == 0) {
			_reported.insert(hk->ID);
			_sink->TriggerLog(LOG_ERROR, "Unable to register " + hk->Description + ": " + error);
		}
	}
}

bool Triggers::OnHotkey(int id) {
	// A registered hotkey was pressed
	if (_paused || _registered.count(id) == 0) {
		return false;
	}
	_sink->Fire(FIRE_HOTKEY, id, "");
	return true;
}

bool Triggers::MatchHotstring(const HotstringBinding& hs, char32_t endChar, const WindowInfo& foreground) const {
	// Does the end of the buffer (minus the end character, if any) hold this
	// trigger, starting on a word boundary?
	size_t len = _buffer.size() - (endChar ? 1 : 0);
	size_t trig = hs.Trigger.size();
	if (trig == 0 || trig > len) {
		return false;
	}
	size_t start = len - trig;
	for (size_t i = 0; i < trig; i++) {
		char32_t a = _buffer[start + i];
		char32_t b = hs.Trigger[i];
		if (hs.CaseSensitive ? a != b : FoldCase(a) != FoldCase(b)) {
			return false;
		}
	}
	if (start > 0 && IsWordChar(_buffer[start - 1]) && IsWordChar(hs.Trigger[0])) {
		return false;
	}
	return hs.When.IsEmpty() || hs.When.Matches(foreground);
}

void Triggers::OnChar(char32_t c, const WindowInfo& foreground) {
	// A character was typed by the user (never by us)
	if (_paused || _bindings.Hotstrings.empty()) {
		return;
	}
	_buffer += c;
	if (_buffer.size() > HOTSTRING_BUFFER) {
		_buffer.erase(0, _buffer.size() - HOTSTRING_BUFFER);
	}

	bool isEnd = _endChars.find(c) != u32string::npos;
	for (auto& hs : _bindings.Hotstrings) {
		bool hit = false;
		string extra;
		if (hs.Immediate) {
			hit = MatchHotstring(hs, 0, foreground);
		}
		else if (isEnd) {
			hit = MatchHotstring(hs, c, foreground);
			extra = U32ToUtf8(c);
		}
		if (hit) {
			_buffer.clear();
			_sink->Fire(FIRE_HOTSTRING, hs.ID, extra);
			return;
		}
	}
}

void Triggers::OnBackspace() {
	// Keep the buffer in step with what the user sees
	if (!_buffer.empty()) {
		_buffer.pop_back();
	}
}

void Triggers::ResetBuffer() {
	// Caret moved, a click happened, or similar: forget what was typed
	_buffer.clear();
}

void Triggers::FireWindow(int id, const WindowInfo& window) {
	_sink->Fire(FIRE_WINDOW, id, to_string(window.Handle));
}

void Triggers::OnActivated(const WindowInfo& window) {
	// Another window came to the front
	if (window.Handle == _lastActive || window.Handle == 0) {
		return;
	}
	_lastActive = window.Handle;
	if (_paused) {
		return;
	}
	for (auto& ev : _bindings.WindowEvents) {
		if (ev.Event == WINDOW_ACTIVATE && ev.When.Matches(window)) {
			FireWindow(ev.ID, window);
		}
	}
}

void Triggers::OnWindowOpened(const WindowInfo& window, uint64_t nowMs) {
	// A window appeared.  Bindings that care about the title get another
	// look when it changes, since many programs set it a moment later.
	if (_paused) {
		return;
	}
	PendingWindow pending;
	for (auto& ev : _bindings.WindowEvents) {
		if (ev.Event != WINDOW_OPEN) {
			continue;
		}
		if (ev.When.Matches(window)) {
			FireWindow(ev.ID, window);
		}
		else if (!ev.When.Title.empty()) {
			pending.IDs.push_back(ev.ID);
		}
	}
	if (!pending.IDs.empty()) {
		pending.Deadline = nowMs + WINDOW_TITLE_WAIT;
		_pending[window.Handle] = pending;
	}
}

void Triggers::OnWindowTitle(const WindowInfo& window, uint64_t nowMs) {
	// A window's title changed; it may now match an on_open binding
	auto it = _pending.find(window.Handle);
	if (it == _pending.end()) {
		return;
	}
	if (nowMs > it->second.Deadline || _paused) {
		_pending.erase(it);
		return;
	}
	vector<int> waiting;
	for (int id : it->second.IDs) {
		bool fired = false;
		for (auto& ev : _bindings.WindowEvents) {
			if (ev.ID == id && ev.When.Matches(window)) {
				FireWindow(id, window);
				fired = true;
			}
		}
		if (!fired) {
			waiting.push_back(id);
		}
	}
	if (waiting.empty()) {
		_pending.erase(it);
	}
	else {
		it->second.IDs = waiting;
	}
}

void Triggers::OnWindowClosed(uint64_t handle) {
	_pending.erase(handle);
	if (handle == _lastActive) {
		_lastActive = 0;
	}
}

bool Triggers::IsPendingWindow(uint64_t handle) const {
	return _pending.count(handle) > 0;
}

bool Triggers::WantsWindowEvent(int event) const {
	for (auto& ev : _bindings.WindowEvents) {
		if (ev.Event == event) {
			return true;
		}
	}
	return false;
}

int Triggers::FindCommand(const string& name) const {
	// Look up a named command, 0 if there isn't one
	for (auto& cmd : _bindings.Commands) {
		if (cmd.Name == name) {
			return cmd.ID;
		}
	}
	return 0;
}

bool Triggers::HasHotstrings() const {
	return !_bindings.Hotstrings.empty();
}

bool Triggers::WatchesTitles() const {
	// Only then is it worth following title changes
	for (auto& hk : _bindings.Hotkeys) {
		if (!hk.When.Title.empty()) {
			return true;
		}
	}
	for (auto& hs : _bindings.Hotstrings) {
		if (!hs.When.Title.empty()) {
			return true;
		}
	}
	for (auto& ev : _bindings.WindowEvents) {
		if (ev.Event == WINDOW_OPEN && !ev.When.Title.empty()) {
			return true;
		}
	}
	return false;
}

bool Triggers::HasFilters() const {
	// Any binding that depends on the foreground window
	for (auto& hk : _bindings.Hotkeys) {
		if (!hk.When.IsEmpty()) {
			return true;
		}
	}
	for (auto& hs : _bindings.Hotstrings) {
		if (!hs.When.IsEmpty()) {
			return true;
		}
	}
	return WantsWindowEvent(WINDOW_ACTIVATE);
}

set<int> Triggers::Registered() const {
	return _registered;
}

string Triggers::HotkeyDescription(int id) const {
	// The text the script used for this hotkey, for the log
	for (auto& hk : _bindings.Hotkeys) {
		if (hk.ID == id) {
			return hk.Description;
		}
	}
	return "";
}
