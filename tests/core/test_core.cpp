// Unit tests for the platform neutral core.  Built and run by CTest on every
// platform, including Linux, so they can't use any OS headers.

#include "../../src/core/Text.h"
#include "../../src/core/Triggers.h"
#include "../../src/core/Image.h"
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

	void SetRemaps(const vector<RemapBinding>& active) override {
		Remaps.clear();
		for (auto& rm : active) {
			Remaps[rm.Source] = rm.Target;
		}
		RemapCalls++;
	}

	map<string, string> Remaps;
	int RemapCalls = 0;
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

static RemapBinding Remap(int id, const string& source, const string& target, const string& process = "") {
	RemapBinding ret;
	ret.ID = id;
	ret.Source = source;
	ret.Target = target;
	ret.When.Process = process;
	return ret;
}

static Bitmap Picture(int width, int height, uint32_t color) {
	// A bitmap of one colour, given as 0xRRGGBB
	Bitmap ret;
	ret.Width = width;
	ret.Height = height;
	for (int i = 0; i < width * height; i++) {
		ret.Pixels += (char)(color & 0xFF);
		ret.Pixels += (char)((color >> 8) & 0xFF);
		ret.Pixels += (char)((color >> 16) & 0xFF);
		ret.Pixels += (char)0xFF;
	}
	return ret;
}

static void Paint(Bitmap& bitmap, int x, int y, uint32_t color, int alpha = 0xFF) {
	char* px = &bitmap.Pixels[((size_t)y * bitmap.Width + x) * 4];
	px[0] = (char)(color & 0xFF);
	px[1] = (char)((color >> 8) & 0xFF);
	px[2] = (char)((color >> 16) & 0xFF);
	px[3] = (char)alpha;
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

	// The same binding published again (a macro bound something else) isn't
	// reported again; a new one for the key is
	triggers.SetBindings(b2, other);
	CHECK(sink.Logged.size() == 1);
	Bindings b3;
	b3.Hotkeys.push_back(Hotkey(11, 0, "f7"));
	triggers.SetBindings(b3, other);
	CHECK(sink.Logged.size() == 2);

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

static WindowEventBinding WindowEvent(int id, int event, const string& process, const string& title = "") {
	WindowEventBinding ret;
	ret.ID = id;
	ret.Event = event;
	ret.When.Process = process;
	ret.When.Title = title;
	return ret;
}

static void TestWindowEvents() {
	FakeSink sink;
	Triggers triggers(&sink);
	WindowInfo notepad = Window("notepad.exe", "Untitled - Notepad");
	notepad.Handle = 100;
	WindowInfo other = Window("code.exe", "main.cpp");
	other.Handle = 200;

	Bindings b;
	b.WindowEvents.push_back(WindowEvent(1, WINDOW_ACTIVATE, "notepad.exe"));
	b.WindowEvents.push_back(WindowEvent(2, WINDOW_ACTIVATE, ""));
	b.WindowEvents.push_back(WindowEvent(3, WINDOW_OPEN, "", "Save As"));
	b.WindowEvents.push_back(WindowEvent(4, WINDOW_OPEN, "code.exe"));
	triggers.SetBindings(b, notepad);
	CHECK(triggers.WantsWindowEvent(WINDOW_OPEN));
	CHECK(triggers.HasFilters());
	CHECK(triggers.WatchesTitles());

	// What was in front when the bindings arrived doesn't count as a change
	triggers.OnActivated(notepad);
	CHECK(sink.Fired.empty());
	triggers.OnActivated(other);
	CHECK(sink.Fired == vector<string>({"4:2:200"}));
	triggers.OnActivated(other);
	CHECK(sink.Fired.size() == 1);
	triggers.OnActivated(notepad);
	CHECK(sink.Fired.size() == 3 && sink.Fired[1] == "4:1:100" && sink.Fired[2] == "4:2:100");

	// A window that matches at once fires at once; one waiting on its title
	// fires when the title arrives, if it arrives in time
	sink.Fired.clear();
	WindowInfo dialog = Window("code.exe", "");
	dialog.Handle = 300;
	triggers.OnWindowOpened(dialog, 1000);
	CHECK(sink.Fired == vector<string>({"4:4:300"}));
	CHECK(triggers.IsPendingWindow(300));
	dialog.Title = "Loading";
	triggers.OnWindowTitle(dialog, 1500);
	CHECK(sink.Fired.size() == 1);
	dialog.Title = "Save As";
	triggers.OnWindowTitle(dialog, 1600);
	CHECK(sink.Fired.size() == 2 && sink.Fired[1] == "4:3:300");
	CHECK(!triggers.IsPendingWindow(300));

	WindowInfo late = Window("x.exe", "");
	late.Handle = 400;
	triggers.OnWindowOpened(late, 1000);
	late.Title = "Save As";
	triggers.OnWindowTitle(late, 5000);
	CHECK(sink.Fired.size() == 2);
	CHECK(!triggers.IsPendingWindow(400));

	triggers.OnWindowOpened(late, 6000);
	triggers.OnWindowClosed(400);
	CHECK(!triggers.IsPendingWindow(400));

	// New bindings (a macro bound something) don't end the wait for a title
	WindowInfo slow = Window("y.exe", "");
	slow.Handle = 500;
	triggers.OnWindowOpened(slow, 8000);
	triggers.SetBindings(b, notepad);
	CHECK(triggers.IsPendingWindow(500));
	Bindings none;
	triggers.SetBindings(none, notepad);
	CHECK(!triggers.IsPendingWindow(500));
	triggers.SetBindings(b, notepad);

	// Paused: nothing fires
	sink.Fired.clear();
	triggers.SetPaused(true, notepad);
	triggers.OnActivated(other);
	triggers.OnWindowOpened(dialog, 7000);
	CHECK(sink.Fired.empty());
}

static void TestRemaps() {
	FakeSink sink;
	Triggers triggers(&sink);
	WindowInfo notepad = Window("notepad.exe", "Untitled - Notepad");
	WindowInfo other = Window("code.exe", "main.cpp");

	Bindings b;
	b.Remaps.push_back(Remap(1, "capslock", "escape"));
	b.Remaps.push_back(Remap(2, "f1", ""));
	b.Remaps.push_back(Remap(3, "capslock", "ctrl", "notepad.exe"));
	triggers.SetBindings(b, other);
	CHECK(triggers.HasRemaps());
	CHECK(sink.Remaps == (map<string, string>{ { "capslock", "escape" }, { "f1", "" } }));

	// The filtered remap wins in its window, and gives way elsewhere
	triggers.OnForeground(notepad);
	CHECK(sink.Remaps["capslock"] == "ctrl");
	int calls = sink.RemapCalls;
	triggers.OnTitleChange(notepad);
	CHECK(sink.RemapCalls == calls);
	triggers.OnForeground(other);
	CHECK(sink.Remaps["capslock"] == "escape");

	// Paused, no key is remapped
	triggers.SetPaused(true, other);
	CHECK(sink.Remaps.empty());
	triggers.SetPaused(false, other);
	CHECK(sink.Remaps.size() == 2);

	Bindings none;
	triggers.SetBindings(none, other);
	CHECK(sink.Remaps.empty());
	CHECK(!triggers.HasRemaps());
}

static void TestMouseHotkeys() {
	CHECK(IsMouseTrigger("mousex1") && IsMouseTrigger("wheelup"));
	CHECK(!IsMouseTrigger("left") && !IsMouseTrigger("mouse"));
	FakeSink sink;
	Triggers triggers(&sink);
	Bindings b;
	b.Hotkeys.push_back(Hotkey(1, MOD_KEY_CTRL, "z"));
	triggers.SetBindings(b, Window("a.exe", ""));
	CHECK(!triggers.HasMouseHotkeys());
	b.Hotkeys.push_back(Hotkey(2, MOD_KEY_CTRL, "mousemiddle"));
	triggers.SetBindings(b, Window("a.exe", ""));
	CHECK(triggers.HasMouseHotkeys());
	CHECK(triggers.Registered() == set<int>({1, 2}));
}

static void TestImages() {
	Bitmap screen = Picture(20, 10, 0x102030);
	Bitmap needle = Picture(3, 2, 0x102030);
	Paint(needle, 1, 0, 0xFF0000);
	vector<pair<int, int>> found;
	CHECK(!FindImage(screen, needle, 0, 10, found));

	// Two copies, found top to bottom, left to right
	Paint(screen, 15, 7, 0xFF0000);
	Paint(screen, 5, 2, 0xFF0000);
	CHECK(FindImage(screen, needle, 0, 10, found));
	CHECK(found == (vector<pair<int, int>>{ { 4, 2 }, { 14, 7 } }));
	CHECK(FindImage(screen, needle, 0, 1, found) && found.size() == 1);

	// Close colours match within the tolerance
	Paint(screen, 15, 7, 0xF80406);
	CHECK(FindImage(screen, needle, 0, 10, found) && found.size() == 1);
	CHECK(FindImage(screen, needle, 8, 10, found) && found.size() == 2);

	// Transparent needle pixels match anything
	Bitmap ring = Picture(3, 3, 0x102030);
	Paint(ring, 1, 1, 0x00FF00, 0);
	Paint(screen, 1, 1, 0x00FF00);
	CHECK(FindImage(screen, ring, 0, 10, found) && found[0] == make_pair(0, 0));

	// A needle bigger than the screen, or an empty one, isn't found
	CHECK(!FindImage(needle, screen, 0, 10, found));
	CHECK(!FindImage(screen, Bitmap(), 0, 10, found));
}

static uint32_t ColorAt(const Bitmap& bitmap, int x, int y) {
	const uint8_t* px = (const uint8_t*)&bitmap.Pixels[((size_t)y * bitmap.Width + x) * 4];
	return (uint32_t)px[2] << 16 | (uint32_t)px[1] << 8 | px[0];
}

static void Line(Bitmap& bitmap, int x1, int y1, int x2, int y2, uint32_t color) {
	for (int y = y1; y <= y2; y++) {
		for (int x = x1; x <= x2; x++) {
			Paint(bitmap, x, y, color);
		}
	}
}

static void TestOcrPrep() {
	// A box with a letter's crossbar and stem inside it, an underline, a
	// thick bar, and a long line that isn't part of a box
	Bitmap page = Picture(80, 60, 0xFFFFFF);
	Line(page, 10, 5, 49, 5, 0x767676);
	Line(page, 10, 24, 49, 24, 0x767676);
	Line(page, 10, 5, 10, 24, 0x767676);
	Line(page, 49, 5, 49, 24, 0x767676);
	Line(page, 20, 14, 29, 14, 0);
	Line(page, 35, 10, 35, 18, 0);
	Line(page, 10, 32, 45, 33, 0x0000EE);
	Line(page, 10, 44, 60, 47, 0);
	Line(page, 70, 2, 70, 57, 0);
	// A focused text box: a 2 pixel ring around its 1 pixel border
	Line(page, 12, 37, 60, 38, 0x101010);
	Line(page, 12, 39, 60, 39, 0x767676);
	Bitmap clean = PrepareForOcr(page, 1);
	CHECK(clean.Width == 80 && clean.Height == 60);
	// The box and the underline are gone
	CHECK(ColorAt(clean, 30, 5) == 0xFFFFFF && ColorAt(clean, 30, 24) == 0xFFFFFF);
	CHECK(ColorAt(clean, 10, 15) == 0xFFFFFF && ColorAt(clean, 49, 15) == 0xFFFFFF);
	CHECK(ColorAt(clean, 20, 32) == 0xFFFFFF && ColorAt(clean, 20, 33) == 0xFFFFFF);
	// Letters, the thick bar, and the lone line stay
	CHECK(ColorAt(clean, 25, 14) == 0 && ColorAt(clean, 35, 14) == 0);
	CHECK(ColorAt(clean, 30, 45) == 0 && ColorAt(clean, 30, 46) == 0);
	CHECK(ColorAt(clean, 70, 30) == 0);
	CHECK(ColorAt(clean, 30, 37) == 0xFFFFFF && ColorAt(clean, 30, 38) == 0xFFFFFF && ColorAt(clean, 30, 39) == 0xFFFFFF);

	// Scaled up, blending between pixels
	Bitmap two = Picture(2, 1, 0);
	Paint(two, 1, 0, 0xFFFFFF);
	Bitmap big = PrepareForOcr(two, 2);
	CHECK(big.Width == 4 && big.Height == 2);
	CHECK(ColorAt(big, 0, 0) == 0 && ColorAt(big, 3, 1) == 0xFFFFFF);
	CHECK(ColorAt(big, 1, 0) == 0x404040 && ColorAt(big, 2, 1) == 0xBFBFBF);
	CHECK(PrepareForOcr(page, 2).Width == 160);
	CHECK(PrepareForOcr(Bitmap(), 2).Width == 0);
}

int RunUpdateCoreTests(int& checks);

int main() {
	TestText();
	TestHotkeys();
	TestHotstrings();
	TestCommands();
	TestWindowEvents();
	TestRemaps();
	TestMouseHotkeys();
	TestImages();
	TestOcrPrep();
	_failures += RunUpdateCoreTests(_checks);
	printf("%d checks, %d failures\n", _checks, _failures);
	return _failures == 0 ? 0 : 1;
}
