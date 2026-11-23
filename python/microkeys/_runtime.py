# The script runtime.  The MicroKeys app calls these functions on its
# script thread: startup() once, then reload(), fire() and run_command() as
# things happen.  Scripts don't use this module.

import builtins
import importlib
import json
import os
import sys
import threading
import time
import traceback
import types

from ._native import native, LOG_INFO, LOG_OUTPUT, LOG_ERROR, FIRE_HOTKEY, FIRE_HOTSTRING, FIRE_COMMAND
from . import _bindings, _keyspec, _settings

_PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))

STARTER_SCRIPT = '''\
# Welcome to MicroKeys.  Every .py file in this folder is loaded when
# MicroKeys starts, and again whenever one of them is saved.  Files that
# start with "_" aren't loaded on their own, so use them for helpers you
# import.  Press Ctrl+Alt+Backspace to stop a macro that runs away.

import time
from microkeys import hotkey, hotstring, command, keys, clip, windows, ui

# Ctrl+Alt+D types today's date
@hotkey("ctrl+alt+d")
def type_date():
    keys.type(time.strftime("%Y-%m-%d"))

# Typing ";shrug" replaces it with the shrug emoticon
hotstring(";shrug", "\\u00af\\\\_(\\u30c4)_/\\u00af")

# Select a formula like 12*7, press Ctrl+Alt+E, and it's replaced by its value
@hotkey("ctrl+alt+e")
def evaluate():
    text = clip.copy_selection()
    if text:
        keys.type(f"{text} = {eval(text)}")

# F8 only does something in Notepad; elsewhere it's left alone
@hotkey("f8", when="notepad.exe")
def notepad_banner():
    keys.type("-" * 40 + "\\n")

# Run from the command line with: MicroKeys.exe run hello
@command("hello")
def hello():
    ui.msgbox("Hello from MicroKeys!")
'''


class _LogWriter:
    # sys.stdout and sys.stderr: whole lines go to the MicroKeys log
    encoding = "utf-8"
    errors = "replace"

    def __init__(self, level):
        self.level = level
        self.pending = ""

    def write(self, text):
        if not isinstance(text, str):
            text = str(text)
        self.pending += text
        while "\n" in self.pending:
            line, self.pending = self.pending.split("\n", 1)
            native.log(self.level, line.rstrip("\r"))
        return len(text)

    def flush(self):
        if self.pending:
            native.log(self.level, self.pending)
            self.pending = ""

    def isatty(self):
        return False

    def writable(self):
        return True


class _Script:
    # The last version of one script file that loaded cleanly
    def __init__(self, filename, module, collector):
        self.filename = filename
        self.module = module
        self.collector = collector


_config = {}
_script_dir = None
_scripts = {}
_actions = {}
_commands = {}


def _log(text, level=LOG_INFO):
    native.log(level, text)


def _error(text):
    native.log(LOG_ERROR, text)


def _is_ours(fn):
    return os.path.normcase(os.path.abspath(fn)).startswith(os.path.normcase(_PACKAGE_DIR) + os.sep)


def _format_exception(exc):
    # A traceback that starts at the user's code, not at ours
    frames = traceback.extract_tb(exc.__traceback__)
    start = 0
    while start < len(frames) and (_is_ours(frames[start].filename) or frames[start].filename == "<string>"):
        start += 1
    lines = []
    if start < len(frames):
        lines.append("Traceback (most recent call last):\n")
        lines.extend(traceback.format_list(frames[start:]))
    lines.extend(traceback.format_exception_only(type(exc), exc))
    return "".join(lines).rstrip("\n")


def _write_editor_config(script_dir, lib_dir):
    # Point Pylance / Pyright at this version's microkeys package so editors
    # can autocomplete it.  Only touches a pyrightconfig.json MicroKeys wrote.
    fn = os.path.join(script_dir, "pyrightconfig.json")
    want = {"extraPaths": [lib_dir], "pythonVersion": f"{sys.version_info[0]}.{sys.version_info[1]}"}
    if os.path.isfile(fn):
        try:
            with open(fn, "rt", encoding="utf-8") as f:
                have = json.load(f)
            if have == want:
                return
            paths = have.get("extraPaths", [])
            if not (len(paths) == 1 and os.path.basename(os.path.dirname(paths[0])).startswith("app-")):
                return
        except Exception:
            return
    try:
        with open(fn, "wt", encoding="utf-8") as f:
            json.dump(want, f, indent=4)
            f.write("\n")
    except OSError:
        pass


def startup(config):
    _config.update(config)
    sys.stdout = _LogWriter(LOG_OUTPUT)
    sys.stderr = _LogWriter(LOG_ERROR)
    sys.argv = ["MicroKeys"]
    sys.dont_write_bytecode = True
    _install_sleep()
    _configure(first=True)
    _log(f"MicroKeys {native.version()}, Python {sys.version.split()[0]}")
    _log(f"Scripts: {_script_dir}")
    return reload(settings=False)


def _install_sleep():
    # time.sleep() on the script thread becomes one the abort hotkey can end,
    # since a Windows interrupt doesn't wake a sleeping thread
    real = time.sleep
    script_thread = threading.get_ident()

    def sleep(seconds):
        if threading.get_ident() != script_thread:
            return real(seconds)
        if seconds < 0:
            raise ValueError("sleep length must be non-negative")
        native.sleep(float(seconds))

    sleep.__doc__ = real.__doc__
    time.sleep = sleep


def _configure(first=False):
    # Read the settings and pass on the ones the app acts on.  Runs again on
    # every reload, so edits to settings.json take effect then.
    global _script_dir
    settings = _settings.load(_config, _error)
    script_dir = _config.get("script_dir") or settings.get("script_folder") or os.path.join(_config["documents_dir"], "MicroKeys")
    script_dir = os.path.abspath(os.path.expandvars(os.path.expanduser(script_dir)))

    if not os.path.isdir(script_dir):
        os.makedirs(script_dir, exist_ok=True)
        if not _config.get("headless"):
            with open(os.path.join(script_dir, "MicroKeys.py"), "wt", encoding="utf-8") as f:
                f.write(STARTER_SCRIPT)
            _log(f"Created a starter script in {script_dir}")
            native.toast("MicroKeys is running", f"It lives in the notification area. Your scripts are in {script_dir}")
    if not _config.get("headless"):
        _write_editor_config(script_dir, _config["lib_dir"])
    if _script_dir is not None and _script_dir in sys.path:
        sys.path.remove(_script_dir)
    sys.path.insert(0, script_dir)
    _script_dir = script_dir
    # Relative paths in scripts mean the script folder
    if not _config.get("headless"):
        os.chdir(script_dir)

    try:
        mods, key, _ = _keyspec.parse_hotkey(settings["abort_hotkey"])
    except Exception as e:
        _error(f"Bad abort_hotkey setting ({e}), using Ctrl+Alt+Backspace")
        mods, key, _ = _keyspec.parse_hotkey(_settings.DEFAULTS["abort_hotkey"])
    native.configure(script_dir, mods, key, str(settings.get("update_feed") or ""), bool(settings.get("check_updates", True)))


def _script_files():
    # The scripts to load: every .py file in the folder not starting with "_"
    try:
        names = os.listdir(_script_dir)
    except OSError as e:
        _error(f"Unable to read {_script_dir}: {e}")
        return []
    ret = []
    for name in sorted(names, key=str.lower):
        fn = os.path.join(_script_dir, name)
        if name.lower().endswith(".py") and not name.startswith("_") and os.path.isfile(fn):
            ret.append(fn)
    return ret


def _purge_modules():
    # Helpers imported from the script folder are imported fresh on reload
    prefix = os.path.normcase(_script_dir + os.sep)
    for name, module in list(sys.modules.items()):
        fn = getattr(module, "__file__", None)
        if fn and os.path.normcase(os.path.abspath(fn)).startswith(prefix):
            del sys.modules[name]


def _load_file(fn):
    # Load one script.  On failure, its last good version stays in place.
    # Returns an error message, or None.
    name = os.path.basename(fn)
    keeping = " (keeping the previous version)" if fn in _scripts else ""
    try:
        with open(fn, "rb") as f:
            source = f.read().decode("utf-8-sig")
        code = compile(source, fn, "exec")
    except SyntaxError as e:
        _error(f"Error in {name}{keeping}:\n" + _format_exception(e))
        return f"{name}: {e.msg} on line {e.lineno}"
    except (OSError, UnicodeDecodeError) as e:
        _error(f"Unable to read {name}{keeping}: {e}")
        return f"{name}: {e}"

    module = types.ModuleType(os.path.splitext(name)[0])
    module.__file__ = fn
    module.__builtins__ = builtins
    collector = _bindings.begin(fn)
    try:
        exec(code, module.__dict__)
    except BaseException as e:
        _error(f"Error in {name}{keeping}:\n" + _format_exception(e))
        return f"{name}: {type(e).__name__}: {e}"
    finally:
        _bindings.end()
    _scripts[fn] = _Script(fn, module, collector)
    return None


def _publish():
    # Hand every binding from every loaded script to the app.  IDs are handed
    # out fresh each time; the app drops the old set before taking the new one.
    hotkeys, hotstrings, commands = [], [], []
    _actions.clear()
    _commands.clear()
    next_id = 1
    seen = {}

    for fn in sorted(_scripts, key=str.lower):
        collector = _scripts[fn].collector
        for hk in collector.hotkeys:
            when = hk.when.fields() if hk.when else ("", "", "")
            sig = ("hotkey", hk.mods, hk.key, when)
            if sig in seen:
                _error(f"{hk.description} at {hk.source} is already bound at {seen[sig]}, ignoring it")
                continue
            seen[sig] = hk.source
            _actions[next_id] = (FIRE_HOTKEY, hk)
            hotkeys.append((next_id, hk.mods, hk.key, hk.description) + when)
            next_id += 1
        for hs in collector.hotstrings:
            when = hs.when.fields() if hs.when else ("", "", "")
            sig = ("hotstring", hs.trigger if hs.case_sensitive else hs.trigger.lower(), when)
            if sig in seen:
                _error(f"Hotstring {hs.trigger!r} at {hs.source} is already bound at {seen[sig]}, ignoring it")
                continue
            seen[sig] = hs.source
            _actions[next_id] = (FIRE_HOTSTRING, hs)
            hotstrings.append((next_id, hs.trigger, hs.immediate, hs.case_sensitive) + when)
            next_id += 1
        for cmd in collector.commands:
            if cmd.name in _commands:
                _error(f"Command {cmd.name!r} at {cmd.source} is already defined at {_commands[cmd.name].source}, ignoring it")
                continue
            _commands[cmd.name] = cmd
            _actions[next_id] = (FIRE_COMMAND, cmd)
            commands.append((next_id, cmd.name))
            next_id += 1

    native.set_bindings(hotkeys, hotstrings, commands)
    return len(hotkeys), len(hotstrings), len(commands)


def reload(settings=True):
    if settings:
        _configure()
    files = _script_files()
    _purge_modules()
    importlib.invalidate_caches()
    errors = []
    for fn in files:
        err = _load_file(fn)
        if err is not None:
            errors.append(err)
    for fn in list(_scripts):
        if fn not in files:
            del _scripts[fn]

    hotkeys, hotstrings, commands = _publish()
    counts = f"{hotkeys} hotkey{'s' if hotkeys != 1 else ''}, {hotstrings} hotstring{'s' if hotstrings != 1 else ''}, {commands} command{'s' if commands != 1 else ''}"
    if errors:
        summary = "; ".join(errors)
        _error(f"Loaded with errors: {summary}")
        native.scripts_loaded(False, summary)
    else:
        loaded = len(files)
        _log(f"Loaded {loaded} script{'s' if loaded != 1 else ''}: {counts}")
        native.scripts_loaded(True, counts)
    return not errors


def _quietly(func, *args):
    # The abort hotkey can deliver a second KeyboardInterrupt while the first
    # is being handled; it means the same thing, so finish the job anyway
    for attempt in range(5):
        try:
            return func(*args)
        except KeyboardInterrupt:
            pass


def _report(what, e):
    _error(f"Error in {what}:\n" + _format_exception(e))
    native.toast("MicroKeys", f"{what} failed: {type(e).__name__}: {e}")


def _run(func, what):
    # Run a macro, reporting failures in the log and as a notification
    _quietly(native.macro_running, what)
    ok = True
    try:
        func()
    except KeyboardInterrupt:
        _quietly(_log, f"Stopped {what}")
    except SystemExit:
        pass
    except BaseException as e:
        ok = False
        _quietly(_report, what, e)
    finally:
        _quietly(native.macro_running, "")
    return ok


def _erase(count):
    from . import keys
    keys.press(*(["backspace"] * count))


def fire(kind, id, extra):
    entry = _actions.get(id)
    if entry is None or entry[0] != kind:
        return True
    kind, item = entry
    if kind == FIRE_HOTKEY:
        what = f"{item.func.__name__} ({item.description})"

        def go():
            native.wait_modifiers(2.0)
            item.func()
        return _run(go, what)

    if kind == FIRE_HOTSTRING:
        from . import keys
        what = f"hotstring {item.trigger!r}"

        def go():
            _erase(len(item.trigger) + len(extra))
            if item.replacement is not None:
                keys.type(item.replacement + extra)
            else:
                item.func()
                if extra:
                    keys.type(extra)
        return _run(go, what)

    if kind == FIRE_COMMAND:
        return _run(item.func, f"command {item.name!r}")
    return True


def run_command(name):
    cmd = _commands.get(name)
    if cmd is None:
        _error(f"No command named {name!r}")
        return False
    _log(f"Running command {name!r}")
    return _run(cmd.func, f"command {name!r}")


def shutdown():
    for stream in (sys.stdout, sys.stderr):
        if isinstance(stream, _LogWriter):
            stream.flush()
