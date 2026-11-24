#include "PythonInclude.h"
#include "ScriptHost.h"
#include "Text.h"
#include "version.h"

// The _microkeys module: the thin layer between the microkeys Python package
// and the platform interface.  Everything user facing lives in the package;
// this file only converts arguments and releases the GIL around calls that
// can take a while.

#define HOST _scriptHost
#define PLAT _scriptHost->Platform

static PyObject* Str(const string& text) {
	// UTF-8 to a Python str, never failing on bad input
	return PyUnicode_DecodeUTF8(text.data(), (Py_ssize_t)text.size(), "replace");
}

class NativeWait {
	// Marks a stretch where the script thread waits in our code, which
	// watches for a cancel itself
public:
	NativeWait() {
		HOST->EnterNative();
	}
	~NativeWait() {
		HOST->LeaveNative();
	}
};

static bool RaiseIfCancelled() {
	// Turn a cancelled macro into KeyboardInterrupt
	if (HOST->IsCancelled()) {
		PyErr_SetNone(PyExc_KeyboardInterrupt);
		return true;
	}
	return false;
}

static PyObject* WindowDict(const WindowInfo& info) {
	// The dict the Python Window class is built from
	if (info.Handle == 0) {
		Py_RETURN_NONE;
	}
	PyObject* title = Str(info.Title);
	PyObject* cls = Str(info.ClassName);
	PyObject* process = Str(info.Process);
	PyObject* ret = Py_BuildValue("{s:K,s:O,s:O,s:O,s:k}",
		"handle", (unsigned long long)info.Handle,
		"title", title,
		"class_name", cls,
		"process", process,
		"pid", (unsigned long)info.Pid);
	Py_XDECREF(title);
	Py_XDECREF(cls);
	Py_XDECREF(process);
	return ret;
}

static bool ReadFilter(const char* process, const char* title, const char* cls, WindowFilter& filter) {
	filter.Process = process;
	filter.Title = title;
	filter.ClassName = cls;
	return true;
}

static PyObject* mk_version(PyObject* self, PyObject* args) {
	return PyUnicode_FromString(VERSION_TEXT);
}

static PyObject* mk_platform(PyObject* self, PyObject* args) {
	return Str(PLAT->PlatformName());
}

static PyObject* mk_log(PyObject* self, PyObject* args) {
	int level;
	const char* text;
	if (!PyArg_ParseTuple(args, "is", &level, &text)) {
		return NULL;
	}
	HOST->Shell->Log(level, text);
	Py_RETURN_NONE;
}

static PyObject* mk_configure(PyObject* self, PyObject* args) {
	ShellConfig config;
	const char* scriptDir;
	const char* abortKey;
	const char* feed;
	int check;
	const char* editor;
	if (!PyArg_ParseTuple(args, "sissps", &scriptDir, &config.AbortMods, &abortKey, &feed, &check, &editor)) {
		return NULL;
	}
	config.ScriptDir = scriptDir;
	config.AbortKey = abortKey;
	config.UpdateFeed = feed;
	config.CheckUpdates = check != 0;
	config.Editor = editor;
	HOST->Shell->Configure(config);
	Py_RETURN_NONE;
}

static PyObject* mk_set_bindings(PyObject* self, PyObject* args) {
	// Hotkeys:    (id, mods, key, description, process, title, class)
	// Hotstrings: (id, trigger, immediate, case_sensitive, process, title, class)
	// Commands:   (id, name)
	// Window events: (id, event, process, title, class)
	PyObject* hotkeys;
	PyObject* hotstrings;
	PyObject* commands;
	PyObject* events;
	if (!PyArg_ParseTuple(args, "O!O!O!O!", &PyList_Type, &hotkeys, &PyList_Type, &hotstrings, &PyList_Type, &commands, &PyList_Type, &events)) {
		return NULL;
	}
	Bindings bindings;
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(hotkeys); i++) {
		HotkeyBinding hk;
		const char *key, *desc, *process, *title, *cls;
		if (!PyArg_ParseTuple(PyList_GET_ITEM(hotkeys, i), "iisssss", &hk.ID, &hk.Mods, &key, &desc, &process, &title, &cls)) {
			return NULL;
		}
		hk.Key = key;
		hk.Description = desc;
		ReadFilter(process, title, cls, hk.When);
		bindings.Hotkeys.push_back(hk);
	}
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(hotstrings); i++) {
		HotstringBinding hs;
		const char *trigger, *process, *title, *cls;
		int immediate, caseSensitive;
		if (!PyArg_ParseTuple(PyList_GET_ITEM(hotstrings, i), "isppsss", &hs.ID, &trigger, &immediate, &caseSensitive, &process, &title, &cls)) {
			return NULL;
		}
		hs.Trigger = Utf8ToU32(trigger);
		hs.Immediate = immediate != 0;
		hs.CaseSensitive = caseSensitive != 0;
		ReadFilter(process, title, cls, hs.When);
		bindings.Hotstrings.push_back(hs);
	}
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(commands); i++) {
		CommandBinding cmd;
		const char* name;
		if (!PyArg_ParseTuple(PyList_GET_ITEM(commands, i), "is", &cmd.ID, &name)) {
			return NULL;
		}
		cmd.Name = name;
		bindings.Commands.push_back(cmd);
	}
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(events); i++) {
		WindowEventBinding ev;
		const char *process, *title, *cls;
		if (!PyArg_ParseTuple(PyList_GET_ITEM(events, i), "iisss", &ev.ID, &ev.Event, &process, &title, &cls)) {
			return NULL;
		}
		ReadFilter(process, title, cls, ev.When);
		bindings.WindowEvents.push_back(ev);
	}
	HOST->Shell->SetBindings(bindings);
	Py_RETURN_NONE;
}

static PyObject* mk_scripts_loaded(PyObject* self, PyObject* args) {
	int ok;
	const char* summary;
	if (!PyArg_ParseTuple(args, "ps", &ok, &summary)) {
		return NULL;
	}
	HOST->Shell->ScriptsLoaded(ok != 0, summary);
	Py_RETURN_NONE;
}

static PyObject* mk_macro_running(PyObject* self, PyObject* args) {
	// What's running now, for the tray icon; "" when nothing is
	const char* what;
	if (!PyArg_ParseTuple(args, "s", &what)) {
		return NULL;
	}
	HOST->Shell->MacroState(what);
	Py_RETURN_NONE;
}

static PyObject* mk_console_state(PyObject* self, PyObject* args) {
	// Whether the console's prompt is in the middle of a block
	int more;
	if (!PyArg_ParseTuple(args, "p", &more)) {
		return NULL;
	}
	HOST->Shell->ConsoleState(more != 0);
	Py_RETURN_NONE;
}

static PyObject* mk_post(PyObject* self, PyObject* args) {
	// Queue a job, from any thread: how the timer thread hands over timers.
	// False when too many jobs are waiting.
	int kind, id;
	const char* extra;
	if (!PyArg_ParseTuple(args, "iis", &kind, &id, &extra)) {
		return NULL;
	}
	return PyBool_FromLong(HOST->PostFire(kind, id, extra));
}

static PyObject* mk_paused(PyObject* self, PyObject* args) {
	return PyBool_FromLong(HOST->Paused);
}

static PyObject* mk_cancelled(PyObject* self, PyObject* args) {
	return PyBool_FromLong(HOST->IsCancelled());
}

static PyObject* mk_check_cancel(PyObject* self, PyObject* args) {
	if (RaiseIfCancelled()) {
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject* mk_sleep(PyObject* self, PyObject* args) {
	double seconds;
	if (!PyArg_ParseTuple(args, "d", &seconds)) {
		return NULL;
	}
	{
		NativeWait wait;
		Py_BEGIN_ALLOW_THREADS
		HOST->Sleep(seconds > 0 ? seconds : 0);
		Py_END_ALLOW_THREADS
	}
	if (RaiseIfCancelled()) {
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject* mk_wait_modifiers(PyObject* self, PyObject* args) {
	// Wait for the user to let go of Ctrl, Alt, Shift, and Win, so they don't
	// mix with the keys a macro sends.  True if they were released in time.
	double timeout;
	if (!PyArg_ParseTuple(args, "d", &timeout)) {
		return NULL;
	}
	bool released = false;
	{
		NativeWait wait;
		Py_BEGIN_ALLOW_THREADS
		for (double waited = 0; waited <= timeout; waited += 0.01) {
			if (!PLAT->ModifiersHeld()) {
				released = true;
				break;
			}
			if (!HOST->Sleep(0.01)) {
				break;
			}
		}
		Py_END_ALLOW_THREADS
	}
	if (RaiseIfCancelled()) {
		return NULL;
	}
	return PyBool_FromLong(released);
}

static PyObject* mk_is_key(PyObject* self, PyObject* args) {
	const char* name;
	if (!PyArg_ParseTuple(args, "s", &name)) {
		return NULL;
	}
	return PyBool_FromLong(PLAT->IsKeyName(name));
}

static PyObject* mk_uses_directinput(PyObject* self, PyObject* args) {
	return PyBool_FromLong(PLAT->UsesDirectInput());
}

static PyObject* mk_send_keys(PyObject* self, PyObject* args) {
	// actions: a list of (kind, value) where kind is 0 for key down, 1 for key
	// up (value is a key name), or 2 for a character (value is one char).
	// Keys stay down for hold ms, and delay ms pass between them.  A frame
	// above 0 means the program uses DirectInput: both are at least frame ms,
	// and characters are typed with their keys.
	PyObject* list;
	int delay, hold, frame;
	if (!PyArg_ParseTuple(args, "O!iii", &PyList_Type, &list, &delay, &hold, &frame)) {
		return NULL;
	}
	vector<KeyAction> actions;
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(list); i++) {
		int kind;
		const char* value;
		if (!PyArg_ParseTuple(PyList_GET_ITEM(list, i), "is", &kind, &value)) {
			return NULL;
		}
		KeyAction action;
		if (kind == 0 || kind == 1) {
			action.Type = kind == 0 ? KeyAction::Down : KeyAction::Up;
			action.Key = value;
		}
		else {
			u32string chars = Utf8ToU32(value);
			if (chars.size() != 1) {
				PyErr_SetString(PyExc_ValueError, "Expected a single character");
				return NULL;
			}
			action.Type = KeyAction::Char;
			action.Character = chars[0];
		}
		actions.push_back(action);
	}
	string error;
	bool ok;
	{
		NativeWait wait;
		Py_BEGIN_ALLOW_THREADS
		ok = PLAT->SendKeys(actions, delay, hold, frame, HOST->CancelFlag, error);
		Py_END_ALLOW_THREADS
	}
	if (RaiseIfCancelled()) {
		return NULL;
	}
	if (!ok) {
		PyErr_SetString(PyExc_ValueError, error.c_str());
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject* mk_cursor(PyObject* self, PyObject* args) {
	int x = 0, y = 0;
	PLAT->GetCursor(x, y);
	return Py_BuildValue("(ii)", x, y);
}

static PyObject* mk_move_cursor(PyObject* self, PyObject* args) {
	int x, y;
	if (!PyArg_ParseTuple(args, "ii", &x, &y)) {
		return NULL;
	}
	PLAT->MoveCursor(x, y);
	Py_RETURN_NONE;
}

static PyObject* mk_mouse_button(PyObject* self, PyObject* args) {
	const char* button;
	int down;
	if (!PyArg_ParseTuple(args, "sp", &button, &down)) {
		return NULL;
	}
	string error;
	if (!PLAT->MouseButton(button, down != 0, error)) {
		PyErr_SetString(PyExc_ValueError, error.c_str());
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject* mk_mouse_wheel(PyObject* self, PyObject* args) {
	int clicks, horizontal;
	if (!PyArg_ParseTuple(args, "ip", &clicks, &horizontal)) {
		return NULL;
	}
	PLAT->MouseWheel(clicks, horizontal != 0);
	Py_RETURN_NONE;
}

static PyObject* mk_clip_get(PyObject* self, PyObject* args) {
	string text;
	if (!PLAT->GetClipboardText(text)) {
		Py_RETURN_NONE;
	}
	return Str(text);
}

static PyObject* mk_clip_set(PyObject* self, PyObject* args) {
	const char* text;
	Py_ssize_t len;
	if (!PyArg_ParseTuple(args, "s#", &text, &len)) {
		return NULL;
	}
	return PyBool_FromLong(PLAT->SetClipboardText(string(text, len)));
}

static PyObject* mk_clip_sequence(PyObject* self, PyObject* args) {
	return PyLong_FromUnsignedLong(PLAT->ClipboardSequence());
}

static PyObject* StrList(const vector<string>& items) {
	PyObject* ret = PyList_New(0);
	for (auto& cur : items) {
		PyObject* item = Str(cur);
		if (item == NULL) {
			Py_DECREF(ret);
			return NULL;
		}
		PyList_Append(ret, item);
		Py_DECREF(item);
	}
	return ret;
}

static bool ReadClipItems(PyObject* list, vector<ClipItem>& items) {
	// A list of (kind, bytes)
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(list); i++) {
		ClipItem item;
		const char* kind;
		const char* data;
		Py_ssize_t len;
		if (!PyArg_ParseTuple(PyList_GET_ITEM(list, i), "sy#", &kind, &data, &len)) {
			return false;
		}
		item.Kind = kind;
		item.Data.assign(data, len);
		items.push_back(item);
	}
	return true;
}

static PyObject* ClipItemList(const vector<ClipItem>& items) {
	PyObject* ret = PyList_New(0);
	for (auto& cur : items) {
		PyObject* kind = Str(cur.Kind);
		PyObject* data = PyBytes_FromStringAndSize(cur.Data.data(), (Py_ssize_t)cur.Data.size());
		PyObject* item = (kind && data) ? PyTuple_Pack(2, kind, data) : NULL;
		Py_XDECREF(kind);
		Py_XDECREF(data);
		if (item == NULL) {
			Py_DECREF(ret);
			return NULL;
		}
		PyList_Append(ret, item);
		Py_DECREF(item);
	}
	return ret;
}

static PyObject* mk_clip_kinds(PyObject* self, PyObject* args) {
	return StrList(PLAT->ClipboardKinds());
}

static PyObject* mk_clip_get_data(PyObject* self, PyObject* args) {
	const char* kind;
	if (!PyArg_ParseTuple(args, "s", &kind)) {
		return NULL;
	}
	string data;
	bool ok;
	Py_BEGIN_ALLOW_THREADS
	ok = PLAT->ReadClipboard(kind, data);
	Py_END_ALLOW_THREADS
	if (!ok) {
		Py_RETURN_NONE;
	}
	return PyBytes_FromStringAndSize(data.data(), (Py_ssize_t)data.size());
}

static PyObject* mk_clip_set_data(PyObject* self, PyObject* args) {
	PyObject* list;
	if (!PyArg_ParseTuple(args, "O!", &PyList_Type, &list)) {
		return NULL;
	}
	vector<ClipItem> items;
	if (!ReadClipItems(list, items)) {
		return NULL;
	}
	string error;
	bool ok;
	Py_BEGIN_ALLOW_THREADS
	ok = PLAT->WriteClipboard(items, error);
	Py_END_ALLOW_THREADS
	if (!ok && !error.empty()) {
		PyErr_SetString(PyExc_ValueError, error.c_str());
		return NULL;
	}
	return PyBool_FromLong(ok);
}

static PyObject* mk_clip_save(PyObject* self, PyObject* args) {
	// Every format on the clipboard, for clip_restore; None if it was busy
	vector<ClipItem> items;
	bool ok;
	Py_BEGIN_ALLOW_THREADS
	ok = PLAT->SaveClipboard(items);
	Py_END_ALLOW_THREADS
	if (!ok) {
		Py_RETURN_NONE;
	}
	return ClipItemList(items);
}

static PyObject* mk_clip_restore(PyObject* self, PyObject* args) {
	PyObject* list;
	if (!PyArg_ParseTuple(args, "O!", &PyList_Type, &list)) {
		return NULL;
	}
	vector<ClipItem> items;
	if (!ReadClipItems(list, items)) {
		return NULL;
	}
	bool ok;
	Py_BEGIN_ALLOW_THREADS
	ok = PLAT->RestoreClipboard(items);
	Py_END_ALLOW_THREADS
	return PyBool_FromLong(ok);
}

static PyObject* mk_clip_concealed(PyObject* self, PyObject* args) {
	// Has a password manager marked what's on the clipboard as private?
	return PyBool_FromLong(PLAT->ClipboardConcealed());
}

static PyObject* mk_list_windows(PyObject* self, PyObject* args) {
	int all;
	if (!PyArg_ParseTuple(args, "p", &all)) {
		return NULL;
	}
	vector<WindowInfo> windows = PLAT->ListWindows(all != 0);
	PyObject* ret = PyList_New(0);
	for (auto& cur : windows) {
		PyObject* item = WindowDict(cur);
		if (item == NULL) {
			Py_DECREF(ret);
			return NULL;
		}
		PyList_Append(ret, item);
		Py_DECREF(item);
	}
	return ret;
}

static PyObject* mk_foreground(PyObject* self, PyObject* args) {
	return WindowDict(PLAT->ForegroundWindow());
}

static PyObject* mk_window_info(PyObject* self, PyObject* args) {
	unsigned long long handle;
	if (!PyArg_ParseTuple(args, "K", &handle)) {
		return NULL;
	}
	WindowInfo info;
	if (!PLAT->GetWindowDetails(handle, info)) {
		Py_RETURN_NONE;
	}
	return WindowDict(info);
}

static PyObject* mk_activate(PyObject* self, PyObject* args) {
	unsigned long long handle;
	if (!PyArg_ParseTuple(args, "K", &handle)) {
		return NULL;
	}
	bool ok;
	{
		NativeWait wait;
		Py_BEGIN_ALLOW_THREADS
		ok = PLAT->ActivateWindow(handle);
		Py_END_ALLOW_THREADS
	}
	return PyBool_FromLong(ok);
}

static PyObject* mk_get_bounds(PyObject* self, PyObject* args) {
	unsigned long long handle;
	if (!PyArg_ParseTuple(args, "K", &handle)) {
		return NULL;
	}
	ScreenRect rect;
	if (!PLAT->GetWindowBounds(handle, rect)) {
		Py_RETURN_NONE;
	}
	return Py_BuildValue("(iiii)", rect.X, rect.Y, rect.Width, rect.Height);
}

static PyObject* mk_set_bounds(PyObject* self, PyObject* args) {
	unsigned long long handle;
	ScreenRect rect;
	if (!PyArg_ParseTuple(args, "Kiiii", &handle, &rect.X, &rect.Y, &rect.Width, &rect.Height)) {
		return NULL;
	}
	return PyBool_FromLong(PLAT->SetWindowBounds(handle, rect));
}

static PyObject* mk_get_state(PyObject* self, PyObject* args) {
	unsigned long long handle;
	if (!PyArg_ParseTuple(args, "K", &handle)) {
		return NULL;
	}
	return Str(PLAT->GetWindowState(handle));
}

static PyObject* mk_set_state(PyObject* self, PyObject* args) {
	unsigned long long handle;
	const char* state;
	if (!PyArg_ParseTuple(args, "Ks", &handle, &state)) {
		return NULL;
	}
	return PyBool_FromLong(PLAT->SetWindowState(handle, state));
}

static PyObject* mk_close_window(PyObject* self, PyObject* args) {
	unsigned long long handle;
	if (!PyArg_ParseTuple(args, "K", &handle)) {
		return NULL;
	}
	return PyBool_FromLong(PLAT->RequestClose(handle));
}

static PyObject* mk_monitors(PyObject* self, PyObject* args) {
	PyObject* ret = PyList_New(0);
	for (auto& cur : PLAT->Monitors()) {
		PyObject* item = Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:O,s:i}",
			"x", cur.Bounds.X, "y", cur.Bounds.Y, "width", cur.Bounds.Width, "height", cur.Bounds.Height,
			"work_x", cur.Work.X, "work_y", cur.Work.Y, "work_width", cur.Work.Width, "work_height", cur.Work.Height,
			"primary", cur.Primary ? Py_True : Py_False, "dpi", cur.Dpi);
		if (item == NULL) {
			Py_DECREF(ret);
			return NULL;
		}
		PyList_Append(ret, item);
		Py_DECREF(item);
	}
	return ret;
}

static PyObject* mk_window_monitor(PyObject* self, PyObject* args) {
	unsigned long long handle;
	if (!PyArg_ParseTuple(args, "K", &handle)) {
		return NULL;
	}
	return PyLong_FromLong(PLAT->WindowMonitor(handle));
}

static PyObject* mk_msgbox(PyObject* self, PyObject* args) {
	const char *text, *title, *buttons;
	if (!PyArg_ParseTuple(args, "sss", &text, &title, &buttons)) {
		return NULL;
	}
	string ret;
	{
		NativeWait wait;
		Py_BEGIN_ALLOW_THREADS
		ret = PLAT->ShowMessage(text, title, buttons);
		Py_END_ALLOW_THREADS
	}
	if (RaiseIfCancelled()) {
		return NULL;
	}
	return Str(ret);
}

static PyObject* mk_ask_text(PyObject* self, PyObject* args) {
	const char *prompt, *title, *initial;
	if (!PyArg_ParseTuple(args, "sss", &prompt, &title, &initial)) {
		return NULL;
	}
	string result;
	bool ok;
	{
		NativeWait wait;
		Py_BEGIN_ALLOW_THREADS
		ok = PLAT->AskText(prompt, title, initial, result);
		Py_END_ALLOW_THREADS
	}
	if (RaiseIfCancelled()) {
		return NULL;
	}
	if (!ok) {
		Py_RETURN_NONE;
	}
	return Str(result);
}

static PyObject* mk_ask_choice(PyObject* self, PyObject* args) {
	PyObject* list;
	const char *prompt, *title;
	if (!PyArg_ParseTuple(args, "O!ss", &PyList_Type, &list, &prompt, &title)) {
		return NULL;
	}
	vector<string> items;
	for (Py_ssize_t i = 0; i < PyList_GET_SIZE(list); i++) {
		const char* item = PyUnicode_AsUTF8(PyList_GET_ITEM(list, i));
		if (item == NULL) {
			return NULL;
		}
		items.push_back(item);
	}
	int ret;
	{
		NativeWait wait;
		Py_BEGIN_ALLOW_THREADS
		ret = PLAT->AskChoice(items, prompt, title);
		Py_END_ALLOW_THREADS
	}
	if (RaiseIfCancelled()) {
		return NULL;
	}
	return PyLong_FromLong(ret);
}

static PyObject* mk_toast(PyObject* self, PyObject* args) {
	const char *title, *text;
	if (!PyArg_ParseTuple(args, "ss", &title, &text)) {
		return NULL;
	}
	PLAT->Toast(title, text);
	Py_RETURN_NONE;
}

static PyObject* mk_overlay(PyObject* self, PyObject* args) {
	const char* text;
	int timeoutMs;
	if (!PyArg_ParseTuple(args, "si", &text, &timeoutMs)) {
		return NULL;
	}
	PLAT->ShowOverlay(text, timeoutMs);
	Py_RETURN_NONE;
}

static PyObject* mk_shell_open(PyObject* self, PyObject* args) {
	const char* target;
	if (!PyArg_ParseTuple(args, "s", &target)) {
		return NULL;
	}
	string error;
	bool ok;
	{
		NativeWait wait;
		Py_BEGIN_ALLOW_THREADS
		ok = PLAT->ShellOpen(target, error);
		Py_END_ALLOW_THREADS
	}
	if (!ok) {
		PyErr_SetString(PyExc_OSError, error.c_str());
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyMethodDef _methods[] = {
	{ "version", mk_version, METH_NOARGS, NULL },
	{ "platform", mk_platform, METH_NOARGS, NULL },
	{ "log", mk_log, METH_VARARGS, NULL },
	{ "configure", mk_configure, METH_VARARGS, NULL },
	{ "set_bindings", mk_set_bindings, METH_VARARGS, NULL },
	{ "scripts_loaded", mk_scripts_loaded, METH_VARARGS, NULL },
	{ "macro_running", mk_macro_running, METH_VARARGS, NULL },
	{ "console_state", mk_console_state, METH_VARARGS, NULL },
	{ "post", mk_post, METH_VARARGS, NULL },
	{ "paused", mk_paused, METH_NOARGS, NULL },
	{ "cancelled", mk_cancelled, METH_NOARGS, NULL },
	{ "check_cancel", mk_check_cancel, METH_NOARGS, NULL },
	{ "sleep", mk_sleep, METH_VARARGS, NULL },
	{ "wait_modifiers", mk_wait_modifiers, METH_VARARGS, NULL },
	{ "is_key", mk_is_key, METH_VARARGS, NULL },
	{ "send_keys", mk_send_keys, METH_VARARGS, NULL },
	{ "uses_directinput", mk_uses_directinput, METH_NOARGS, NULL },
	{ "cursor", mk_cursor, METH_NOARGS, NULL },
	{ "move_cursor", mk_move_cursor, METH_VARARGS, NULL },
	{ "mouse_button", mk_mouse_button, METH_VARARGS, NULL },
	{ "mouse_wheel", mk_mouse_wheel, METH_VARARGS, NULL },
	{ "clip_get", mk_clip_get, METH_NOARGS, NULL },
	{ "clip_set", mk_clip_set, METH_VARARGS, NULL },
	{ "clip_sequence", mk_clip_sequence, METH_NOARGS, NULL },
	{ "clip_kinds", mk_clip_kinds, METH_NOARGS, NULL },
	{ "clip_get_data", mk_clip_get_data, METH_VARARGS, NULL },
	{ "clip_set_data", mk_clip_set_data, METH_VARARGS, NULL },
	{ "clip_save", mk_clip_save, METH_NOARGS, NULL },
	{ "clip_restore", mk_clip_restore, METH_VARARGS, NULL },
	{ "clip_concealed", mk_clip_concealed, METH_NOARGS, NULL },
	{ "list_windows", mk_list_windows, METH_VARARGS, NULL },
	{ "foreground", mk_foreground, METH_NOARGS, NULL },
	{ "window_info", mk_window_info, METH_VARARGS, NULL },
	{ "activate", mk_activate, METH_VARARGS, NULL },
	{ "get_bounds", mk_get_bounds, METH_VARARGS, NULL },
	{ "set_bounds", mk_set_bounds, METH_VARARGS, NULL },
	{ "get_state", mk_get_state, METH_VARARGS, NULL },
	{ "set_state", mk_set_state, METH_VARARGS, NULL },
	{ "close_window", mk_close_window, METH_VARARGS, NULL },
	{ "monitors", mk_monitors, METH_NOARGS, NULL },
	{ "window_monitor", mk_window_monitor, METH_VARARGS, NULL },
	{ "msgbox", mk_msgbox, METH_VARARGS, NULL },
	{ "ask_text", mk_ask_text, METH_VARARGS, NULL },
	{ "ask_choice", mk_ask_choice, METH_VARARGS, NULL },
	{ "toast", mk_toast, METH_VARARGS, NULL },
	{ "overlay", mk_overlay, METH_VARARGS, NULL },
	{ "shell_open", mk_shell_open, METH_VARARGS, NULL },
	{ NULL, NULL, 0, NULL },
};

static struct PyModuleDef _module = {
	PyModuleDef_HEAD_INIT, "_microkeys", NULL, -1, _methods,
};

PyMODINIT_FUNC PyInit__microkeys() {
	return PyModule_Create(&_module);
}
