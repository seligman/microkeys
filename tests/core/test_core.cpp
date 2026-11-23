// Unit tests for the platform neutral core.  Built and run by CTest on every
// platform, including Linux, so they can't use any OS headers.

#include "../../src/core/Text.h"
#include "../../src/core/Triggers.h"
#include <stdio.h>

static int _failures = 0;
static int _checks = 0;

#define CHECK(cond) do { _checks++; if (!(cond)) { _failures++; printf("FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

class FakeSink : public ITriggerSink {
	// Records what the trigger registry asks the platform to do
public:
	set<int> Live;
	set<pair<int, string>> Taken;
	vector<string> Fired;
	vector<string> Logged;
	map<int, pair<int, string>> Combos;

	bool RegisterHotkey(int id, int mods, const string& key, string& error) override {
		auto combo = make_pair(mods, key);
		if (Taken.count(combo) > 0) {
			error = "already in use";
			return false;
		}
		Taken.insert(combo);
		Combos[id] = combo;
		Live.insert(id);
		return true;
	}

	void UnregisterHotkey(int id) override {
		Taken.erase(Combos[id]);
		Live.erase(id);
	}

	void Fire(int kind, int id, const string& extra) override {
		Fired.push_back(to_string(kind) + ":" + to_string(id) + ":" + extra);
	}

	void TriggerLog(int level, const string& text) override {
		Logged.push_back(text);
	}
};

static WindowInfo Window(const string& process, const string& title) {
	// A fake foreground window
	WindowInfo ret;
	ret.Process = process;
	ret.Title = title;
	ret.ClassName = "TestClass";
	return ret;
}

static HotkeyBinding Hotkey(int id, int mods, const string& key, const string& process = "") {
	// A hotkey binding, optionally only for one process
	HotkeyBinding ret;
	ret.ID = id;
	ret.Mods = mods;
	ret.Key = key;
	ret.When.Process = process;
	ret.Description = key;
	return ret;
}

static HotstringBinding Hotstring(int id, const string& trigger, bool immediate = true) {
	// A hotstring binding
	HotstringBinding ret;
	ret.ID = id;
	ret.Trigger = Utf8ToU32(trigger);
	ret.Immediate = immediate;
	return ret;
}

static void Type(Triggers& triggers, const string& text, const WindowInfo& window) {
	// Feed typed text to the registry one character at a time
	for (auto c : Utf8ToU32(text)) {
		triggers.OnChar(c, window);
	}
}

static void TestText() {
	CHECK(Utf8ToU32("caf\xC3\xA9") == U"café");
	CHECK(U32ToUtf8(U"—x\U0001F600") == "\xE2\x80\x94x\xF0\x9F\x98\x80");
	CHECK(Utf8ToU32("a\xFF" "b") == U"a�b");
	CHECK(Utf8ToU32("\xE2\x80") == U"��");
	CHECK(FoldCase('Q') == 'q');
	CHECK(IsWordChar('a') && IsWordChar('7') && !IsWordChar(';'));
	CHECK(GlobMatch("notepad.exe", "Notepad.EXE"));
	CHECK(GlobMatch("*pad*", "Notepad.exe"));
	CHECK(GlobMatch("note?ad.exe", "notepad.exe"));
	CHECK(!GlobMatch("notepad", "notepad.exe"));
	CHECK(GlobMatch("*", ""));
	CHECK(GlobMatch("a*b*c", "aXXbYYc"));
	CHECK(!GlobMatch("a*b*c", "aXXbYY"));
}

static void TestHotkeys() {
	FakeSink sink;
	Triggers triggers(&sink);
	WindowInfo notepad = Window("notepad.exe", "Untitled - Notepad");
	WindowInfo other = Window("code.exe", "main.cpp");

	Bindings b;
	b.Hotkeys.push_back(Hotkey(1, MOD_KEY_CTRL | MOD_KEY_ALT, "z"));
	b.Hotkeys.push_back(Hotkey(2, 0, "f5", "notepad.exe"));
	b.Hotkeys.push_back(Hotkey(3, 0, "f6"));
	b.Hotkeys.push_back(Hotkey(4, 0, "f6", "NOTEPAD.EXE"));
	triggers.SetBindings(b, other);

	// Away from Notepad, the filtered F5 isn't held, and F6 falls back to the global one
	CHECK(triggers.Registered() == set<int>({1, 3}));

	// In Notepad, both filtered bindings take over
	triggers.OnForeground(notepad);
	CHECK(triggers.Registered() == set<int>({1, 2, 4}));
	CHECK(triggers.OnHotkey(4));
	CHECK(!triggers.OnHotkey(3));
	CHECK(sink.Fired.back() == "1:4:");

	triggers.OnForeground(other);
	CHECK(triggers.Registered() == set<int>({1, 3}));

	// Pausing releases everything, resuming takes it back
	triggers.SetPaused(true, other);
	CHECK(triggers.Registered().empty());
	CHECK(sink.Live.empty());
	CHECK(!triggers.OnHotkey(1));
	triggers.SetPaused(false, other);
	CHECK(triggers.Registered() == set<int>({1, 3}));

	// A key held by another program is reported once, not on every focus change
	sink.Taken.insert(make_pair(0, string("f7")));
	Bindings b2;
	b2.Hotkeys.push_back(Hotkey(10, 0, "f7"));
	triggers.SetBindings(b2, other);
	triggers.OnForeground(notepad);
	triggers.OnForeground(other);
	CHECK(sink.Logged.size() == 1);
	CHECK(triggers.Registered().empty());

	// New bindings release the old ones
	CHECK(sink.Live.empty());
	triggers.Clear();
	CHECK(triggers.Registered().empty());
}

static void TestHotstrings() {
	FakeSink sink;
	Triggers triggers(&sink);
	WindowInfo notepad = Window("notepad.exe", "Untitled - Notepad");
	WindowInfo other = Window("code.exe", "main.cpp");

	Bindings b;
	b.Hotstrings.push_back(Hotstring(1, ";sig"));
	b.Hotstrings.push_back(Hotstring(2, "btw", false));
	HotstringBinding only = Hotstring(3, ";np");
	only.When.Process = "notepad.exe";
	b.Hotstrings.push_back(only);
	HotstringBinding exact = Hotstring(4, "CAPS", false);
	exact.CaseSensitive = true;
	b.Hotstrings.push_back(exact);
	triggers.SetBindings(b, other);

	Type(triggers, "hello ;SIG", other);
	CHECK(sink.Fired.size() == 1 && sink.Fired[0] == "2:1:");

	// Needs an ending character, and a word boundary in front
	Type(triggers, "btw", other);
	CHECK(sink.Fired.size() == 1);
	Type(triggers, " ", other);
	CHECK(sink.Fired.size() == 2 && sink.Fired[1] == "2:2: ");
	Type(triggers, "abtw ", other);
	CHECK(sink.Fired.size() == 2);
	Type(triggers, "(btw.", other);
	CHECK(sink.Fired.size() == 3 && sink.Fired[2] == "2:2:.");

	// Backspace edits the buffer
	Type(triggers, ";six", other);
	triggers.OnBackspace();
	Type(triggers, "g", other);
	CHECK(sink.Fired.size() == 4 && sink.Fired[3] == "2:1:");

	// A title change re-checks filters without forgetting what was typed
	Type(triggers, ";si", other);
	triggers.OnTitleChange(other);
	Type(triggers, "g", other);
	CHECK(sink.Fired.size() == 5 && sink.Fired[4] == "2:1:");
	sink.Fired.pop_back();

	// Window filters and focus changes
	Type(triggers, ";np", other);
	CHECK(sink.Fired.size() == 4);
	Type(triggers, ";n", notepad);
	triggers.OnForeground(notepad);
	Type(triggers, "p", notepad);
	CHECK(sink.Fired.size() == 4);
	Type(triggers, ";np", notepad);
	CHECK(sink.Fired.size() == 5 && sink.Fired[4] == "2:3:");

	// Case sensitivity
	Type(triggers, " caps ", other);
	CHECK(sink.Fired.size() == 5);
	Type(triggers, " CAPS ", other);
	CHECK(sink.Fired.size() == 6 && sink.Fired[5] == "2:4: ");

	// Paused means nothing fires
	triggers.SetPaused(true, other);
	Type(triggers, ";sig", other);
	CHECK(sink.Fired.size() == 6);
}

static void TestCommands() {
	FakeSink sink;
	Triggers triggers(&sink);
	Bindings b;
	CommandBinding cmd;
	cmd.ID = 7;
	cmd.Name = "say-hello";
	b.Commands.push_back(cmd);
	triggers.SetBindings(b, Window("", ""));
	CHECK(triggers.FindCommand("say-hello") == 7);
	CHECK(triggers.FindCommand("nope") == 0);
}

int RunUpdateCoreTests(int& checks);

int main() {
	TestText();
	TestHotkeys();
	TestHotstrings();
	TestCommands();
	_failures += RunUpdateCoreTests(_checks);
	printf("%d checks, %d failures\n", _checks, _failures);
	return _failures == 0 ? 0 : 1;
}
