# The script runtime.  The MicroKeys app calls these functions on its
# script thread: startup() once, then reload(), fire(), run_command(),
# console() and action() as things happen.  Scripts don't use this module.

import builtins
import codeop
import importlib
import json
import os
import sys
import threading
import time
import traceback
import types

from ._native import (native, LOG_INFO, LOG_OUTPUT, LOG_ERROR, FIRE_HOTKEY, FIRE_HOTSTRING, FIRE_COMMAND,
                      FIRE_WINDOW, FIRE_TIMER, FIRE_CLIPBOARD, FIRE_MESSAGE)
from . import _bindings, _keyspec, _net, _packages, _settings, _store, _timers, _transfer, clip, net, ui

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
_next_id = 0
_published = None
_scheduler = _timers.Scheduler()
_console_lines = []
_console_ns = {}

# What's copied to the stubs folder for pip install -e, besides the package
STUB_FILES = ["pyproject.toml", "_build_backend.py", "_microkeys.pyi", "VERSION"]


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


def _local_dir():
    return _config.get("local_dir") or _config["data_dir"]


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


def _sync_stubs(lib_dir, target):
    # Keep a copy of the package in a folder that doesn't move with each
    # update, for "pip install -e" to point at.  Outside MicroKeys the package
    # runs against the stand-ins in _offline.
    if not os.path.isfile(os.path.join(lib_dir, "pyproject.toml")):
        return
    want = {}
    for name in STUB_FILES:
        if os.path.isfile(os.path.join(lib_dir, name)):
            want[name] = os.path.join(lib_dir, name)
    package = os.path.join(lib_dir, "microkeys")
    for name in os.listdir(package):
        if name.endswith(".py") or name == "py.typed":
            want[os.path.join("microkeys", name)] = os.path.join(package, name)
    try:
        for rel, src in want.items():
            dst = os.path.join(target, rel)
            with open(src, "rb") as f:
                data = f.read()
            if os.path.isfile(dst):
                with open(dst, "rb") as f:
                    if f.read() == data:
                        continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(data)
        for name in os.listdir(os.path.join(target, "microkeys")):
            if name.endswith(".py") and os.path.join("microkeys", name) not in want:
                os.unlink(os.path.join(target, "microkeys", name))
    except OSError as e:
        _error(f"Unable to update the stub package in {target}: {e}")


def startup(config):
    _config.update(config)
    sys.stdout = _LogWriter(LOG_OUTPUT)
    sys.stderr = _LogWriter(LOG_ERROR)
    sys.argv = ["MicroKeys"]
    sys.dont_write_bytecode = True
    _install_sleep()
    _store.load(os.path.join(_config["data_dir"], "store.json"))
    _configure(first=True)
    _log(f"MicroKeys {native.version()}, Python {sys.version.split()[0]}")
    _log(f"Scripts: {_script_dir}")
    if native.is_elevated():
        _log("Running as administrator")
    return reload(settings=False, starting=True)


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


def _int_setting(settings, name):
    try:
        return int(settings.get(name) or 0)
    except (TypeError, ValueError):
        _error(f"The {name} setting should be a number, using {_settings.DEFAULTS[name]}")
        return _settings.DEFAULTS[name]


def _configure(first=False):
    # Read the settings and pass on the ones the app acts on.  Runs again on
    # every reload, so edits to settings.json take effect then.
    global _script_dir
    settings = _settings.load(_config, _error)
    script_dir = _config.get("script_dir") or settings.get("script_folder") or os.path.join(_config["documents_dir"], "MicroKeys")
    script_dir = os.path.abspath(os.path.expandvars(os.path.expanduser(script_dir)))
    headless = _config.get("headless")

    if not os.path.isdir(script_dir):
        os.makedirs(script_dir, exist_ok=True)
        if not headless:
            with open(os.path.join(script_dir, "MicroKeys.py"), "wt", encoding="utf-8") as f:
                f.write(STARTER_SCRIPT)
            _log(f"Created a starter script in {script_dir}")
            native.toast("MicroKeys is running", f"It lives in the notification area. Your scripts are in {script_dir}")
    if not headless:
        _write_editor_config(script_dir, _config["lib_dir"])
        _sync_stubs(_config["lib_dir"], os.path.join(_local_dir(), "stubs"))
    if _script_dir is not None and _script_dir in sys.path:
        sys.path.remove(_script_dir)
    sys.path.insert(0, script_dir)
    _script_dir = script_dir
    # Relative paths in scripts mean the script folder
    if not headless:
        os.chdir(script_dir)

    try:
        mods, key, _ = _keyspec.parse_hotkey(settings["abort_hotkey"])
    except Exception as e:
        _error(f"Bad abort_hotkey setting ({e}), using Ctrl+Alt+Backspace")
        mods, key, _ = _keyspec.parse_hotkey(_settings.DEFAULTS["abort_hotkey"])
    native.configure(script_dir, mods, key, str(settings.get("update_feed") or ""), bool(settings.get("check_updates", True)),
                     str(settings.get("editor") or ""), bool(settings.get("run_elevated", False)))

    net._data_dir = _config["data_dir"]
    _net.configure(settings, _config["data_dir"], headless)

    # Packages scripts import, installed before the scripts load
    _packages.activate(_local_dir())
    specs = settings.get("packages") or []
    if not isinstance(specs, list) or not all(isinstance(x, str) for x in specs):
        _error('The packages setting should be a list of names, like ["requests"]')
        specs = []
    port = _int_setting(settings, "debug_port")
    if not headless:
        try:
            if specs or port:
                _packages.ensure(_config, specs + (["debugpy"] if port else []))
            _packages.debug(port)
        except KeyboardInterrupt:
            _log("Stopped installing packages")
        except Exception as e:
            _error(f"Unable to install packages: {e}")


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


def _collectors():
    # What macros bound comes first, so it wins over the scripts' own
    # bindings for the same key
    scripts = [_scripts[fn].collector for fn in sorted(_scripts, key=str.lower)]
    return ([_bindings.live] if _bindings.live is not None else []) + scripts


def _handlers(attr):
    return [entry for collector in _collectors() for entry in getattr(collector, attr)]


def _new_id(taken):
    # A binding keeps its id for as long as it exists, and ids aren't reused
    # until they wrap, so a job queued for a binding that has since gone
    # finds nothing.  They stay below 0xC000, where Windows hotkey ids end.
    global _next_id
    while True:
        _next_id = _next_id + 1 if _next_id < 0xBF00 else 1
        if _next_id not in taken and _next_id not in _actions:
            return _next_id


def _publish():
    # Hand every binding to the app, and the timers to the scheduler
    global _published
    hotkeys, hotstrings, commands, events, timers, remaps = [], [], [], [], [], []
    actions = {}
    names = {}
    messages = {}
    seen = {}
    collectors = _collectors()

    def claim(entry, kind, collector):
        if entry.id is None:
            entry.id = _new_id(actions)
        actions[entry.id] = (kind, entry, collector)
        return entry.id

    def clash(sig, entry, live, what):
        # A second binding for the same thing loses.  Only two scripts
        # disagreeing is worth a message; a macro's binding wins quietly.
        if sig in seen:
            if not live and not seen[sig][1]:
                _error(f"{what} at {entry.source} is already bound at {seen[sig][0]}, ignoring it")
            return True
        seen[sig] = (entry.source, live)
        return False

    # A macro's hotkey without a when= takes the key in every window, so the
    # scripts' bindings for it, filtered or not, step aside
    taken = {(hk.mods, hk.key) for hk in (_bindings.live.hotkeys if _bindings.live is not None else []) if hk.when is None}

    for collector in collectors:
        live = collector is _bindings.live
        for hk in collector.hotkeys:
            if not live and (hk.mods, hk.key) in taken:
                continue
            if not clash(("hotkey",) + hk.signature(), hk, live, hk.description):
                when = hk.when.fields() if hk.when else ("", "", "")
                hotkeys.append((claim(hk, FIRE_HOTKEY, collector), hk.mods, hk.key, hk.description) + when)
        for hs in collector.hotstrings:
            if not clash(("hotstring",) + hs.signature(), hs, live, f"Hotstring {hs.trigger!r}"):
                when = hs.when.fields() if hs.when else ("", "", "")
                hotstrings.append((claim(hs, FIRE_HOTSTRING, collector), hs.trigger, hs.immediate, hs.case_sensitive) + when)
        for cmd in collector.commands:
            if not clash(("command", cmd.name), cmd, live, f"Command {cmd.name!r}"):
                names[cmd.name] = cmd
                commands.append((claim(cmd, FIRE_COMMAND, collector), cmd.name))
        for ev in collector.window_events:
            when = ev.when.fields() if ev.when else ("", "", "")
            events.append((claim(ev, FIRE_WINDOW, collector), ev.event) + when)
        for rm in collector.remaps:
            if not clash(("remap",) + rm.signature(), rm, live, f"Remap of {rm.key}"):
                when = rm.when.fields() if rm.when else ("", "", "")
                remaps.append((claim(rm, 0, collector), rm.key, rm.target or "") + when)
        for msg in collector.messages:
            if not clash(("message", msg.name), msg, live, f"Message {msg.name!r}"):
                messages[msg.name] = claim(msg, FIRE_MESSAGE, collector)
        # A one-time timer that has been handed over keeps its id until its
        # job runs; the scheduler leaves it be
        for timer in collector.timers:
            claim(timer, FIRE_TIMER, collector)
            timers.append(timer)

    _actions.clear()
    _actions.update(actions)
    _commands.clear()
    _commands.update(names)
    _bindings.loaded[:] = collectors
    _bindings.changed = False
    lists = (hotkeys, hotstrings, commands, events, remaps)
    if lists != _published:
        native.set_bindings(*lists)
        _published = lists
    if not _config.get("headless"):
        _scheduler.set(timers)
    _net.set_handlers(messages)
    return {"hotkey": len(hotkeys), "hotstring": len(hotstrings), "command": len(commands),
            "timer": len([t for t in timers if not t.spent]), "remap": len(remaps), "window event": len(events),
            "message handler": len(messages)}


def _counts(counts):
    # "3 hotkeys, 2 hotstrings, 1 command", plus the other kinds when there are any
    parts = []
    for name, count in counts.items():
        if count or name in ("hotkey", "hotstring", "command"):
            parts.append(f"{count} {name}{'s' if count != 1 else ''}")
    return ", ".join(parts)


def reload(settings=True, starting=False):
    if settings:
        _configure()
    files = _script_files()
    _purge_modules()
    importlib.invalidate_caches()
    # What macros bound belonged to the old scripts
    _bindings.live = _bindings.Collector(None)
    errors = []
    for fn in files:
        err = _load_file(fn)
        if err is not None:
            errors.append(err)
    for fn in list(_scripts):
        if fn not in files:
            del _scripts[fn]

    counts = _counts(_publish())
    if errors:
        summary = "; ".join(errors)
        _error(f"Loaded with errors: {summary}")
        native.scripts_loaded(False, summary)
    else:
        loaded = len(files)
        _log(f"Loaded {loaded} script{'s' if loaded != 1 else ''}: {counts}")
        native.scripts_loaded(True, counts)

    ok = not errors
    for entry in _handlers("starts" if starting else "reloads"):
        ok = _run(entry.func, f"{entry.what} ({_bindings._name(entry.func)})") and ok
    _finish()
    return ok


def _quietly(func, *args):
    # The abort hotkey can deliver a second KeyboardInterrupt while the first
    # is being handled; it means the same thing, so finish the job anyway
    for attempt in range(5):
        try:
            return func(*args)
        except KeyboardInterrupt:
            pass


def _report(what, e, toast):
    _error(f"Error in {what}:\n" + _format_exception(e))
    if toast:
        native.toast("MicroKeys", f"{what} failed: {type(e).__name__}: {e}")


def _run(func, what, toast=True):
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
        _quietly(_report, what, e, toast)
    finally:
        _quietly(native.macro_running, "")
    return ok


def _finish():
    # After every job: hand over the bindings macros changed, save the store,
    # and let a message the job showed go
    if _bindings.changed:
        _quietly(_publish)
    _quietly(_store.save)
    _quietly(ui._macro_done)


def _erase(count):
    from . import keys
    keys.press(*(["backspace"] * count))


def fire(kind, id, extra):
    try:
        return _fire(kind, id, extra)
    finally:
        # Even if the abort hotkey cut this short, the timer can come round again
        if kind == FIRE_TIMER:
            _quietly(_scheduler.ran, id)
        _finish()


def _fire(kind, id, extra):
    if kind == FIRE_CLIPBOARD:
        return _clipboard_changed()
    if kind == FIRE_TIMER:
        _scheduler.ran(id)
    entry = _actions.get(id)
    if entry is None or entry[0] != kind:
        return True
    kind, item, collector = entry
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

    if kind == FIRE_TIMER:
        if item.once() and item in collector.timers:
            collector.timers.remove(item)
            _bindings.changed = True
        # The abort hotkey dropped the job before it ran
        if extra == "dropped":
            return True
        return _run(item.func, f"timer {item.describe()}")

    if kind == FIRE_MESSAGE:
        try:
            go, what = _net.dispatch(item, extra)
        except (ValueError, KeyError) as e:
            _error(f"A message from another machine was garbled: {e}")
            return False
        return _run(go, what)

    if kind == FIRE_WINDOW:
        from . import windows
        try:
            info = native.window_info(int(extra))
        except ValueError:
            info = None
        if info is None:
            return True
        win = windows.Window(info)
        return _run(lambda: item.func(win), f"{item.what} ({_bindings._name(item.func)})")
    return True


def _clipboard_changed():
    # Another program changed the clipboard
    clip.record(_int_setting(_settings.current, "clipboard_history"))
    if native.paused():
        return True
    ok = True
    for entry in _handlers("clipboard"):
        ok = _run(entry.func, f"on_change ({_bindings._name(entry.func)})") and ok
    return ok


def run_command(name):
    try:
        cmd = _commands.get(name)
        if cmd is None:
            _error(f"No command named {name!r}")
            return False
        _log(f"Running command {name!r}")
        return _run(cmd.func, f"command {name!r}")
    finally:
        _finish()


def console(line):
    # A line typed at the log window's prompt.  Like Python's own prompt, a
    # block such as "for x in y:" runs once a blank line ends it.
    if not line.strip() and not _console_lines:
        return True
    _log((">>> " if not _console_lines else "... ") + line)
    _console_lines.append(line)
    try:
        code = codeop.compile_command("\n".join(_console_lines), "<console>", "single")
    except (SyntaxError, OverflowError, ValueError) as e:
        _console_lines.clear()
        native.console_state(False)
        _error(_format_exception(e))
        return True
    if code is None:
        native.console_state(True)
        return True
    _console_lines.clear()
    native.console_state(False)

    if not _console_ns:
        exec("from microkeys import *\nimport microkeys", _console_ns)
    # Each script under its own name, unless that name is taken by something
    # else, like microkeys' own clip or keys
    prefix = os.path.normcase(_script_dir + os.sep)
    for script in _scripts.values():
        name = script.module.__name__
        have = _console_ns.get(name)
        if have is None or os.path.normcase(getattr(have, "__file__", None) or "").startswith(prefix):
            _console_ns[name] = script.module
    try:
        _run(lambda: exec(code, _console_ns), "the console", toast=False)
    finally:
        _finish()
    return True


def action(name):
    # Something chosen in the app's menus that's done in Python
    try:
        if name == "install-packages":
            done = []
            _run(lambda: done.extend(_install_packages()), "installing packages")
            if done:
                return reload()
            return True
        if name in _MENU_ACTIONS:
            done = []
            _run(lambda: done.append(_MENU_ACTIONS[name]()), _MENU_ACTIONS[name].__doc__.strip())
            if done and done[0]:
                return reload()
            return True
        _error(f"Unknown action {name!r}")
        return False
    finally:
        _finish()


def _install_packages():
    # Ask which packages, install them, and add them to the packages setting
    # so they're there after MicroKeys moves to a new Python
    text = ui.input("Python packages to install from PyPI, separated by spaces:", title="Install Python packages")
    specs = text.split() if text else []
    if not specs:
        return []
    if not _packages.install(_config, specs, upgrade=True):
        ui.msgbox("pip couldn't install " + ", ".join(specs) + ". The log has its messages.", title="Install Python packages")
        return []
    _settings.add_packages(_config, specs, _error)
    _log("Installed " + ", ".join(specs))
    native.toast("MicroKeys", "Installed " + ", ".join(specs))
    return specs


def _new_token():
    """making a pairing token"""
    token = _net.new_token()
    have = _net.read_token(_config["data_dir"])
    if have is not None:
        answer = ui.msgbox("This machine is already paired. A new token means pairing every other machine again.\n\n"
                           "Make a new one anyway?", title="Networking", buttons="yesno")
        if answer != "yes":
            ui.input("This machine's pairing token. Copy it, then choose Networking > Enter a pairing token on the other machine "
                     "and paste it there.", title="Networking", default=have)
            return False
    _net.save_token(_config["data_dir"], token)
    ui.input("Copy this pairing token, then choose Networking > Enter a pairing token on the other machine and paste it there.",
             title="Networking", default=token)
    _log("Made a new pairing token for networking")
    return True


def _enter_token():
    """entering a pairing token"""
    text = ""
    while True:
        text = ui.input("Paste the pairing token from the other machine:", title="Networking", default=text)
        if not text or not text.strip():
            return False
        try:
            token = _net.check_token(text)
        except ValueError as e:
            if ui.msgbox(f"{e}.\n\nTry again?", title="Networking", buttons="yesno") != "yes":
                return False
            continue
        _net.save_token(_config["data_dir"], token)
        _log("Saved the pairing token for networking")
        return True


def _network_off():
    """turning networking off"""
    if _net.read_token(_config["data_dir"]) is None:
        ui.msgbox("Networking is already off.", title="Networking")
        return False
    if ui.msgbox("Turn networking off? This forgets the pairing token, so other machines can't send messages here, "
                 "and this machine can't send to them.", title="Networking", buttons="yesno") != "yes":
        return False
    _net.save_token(_config["data_dir"], None)
    _log("Turned networking off")
    return True


def _export():
    """exporting scripts and settings"""
    path = ui.choose_file(save=True, title="Export scripts and settings", types=[("Zip files", "*.zip")],
                          default=_transfer.default_name())
    if not path:
        return False
    count = _transfer.export(path, _script_dir, _config["data_dir"])
    _log(f"Exported {count} file{'s' if count != 1 else ''}, the settings, and the store to {path}")
    native.toast("MicroKeys", f"Exported the scripts and settings to {os.path.basename(path)}")
    return False


def _import():
    """importing scripts and settings"""
    title = "Import scripts and settings"
    path = ui.choose_file(title=title, types=[("Zip files", "*.zip")])
    if not path:
        return False
    try:
        plan = _transfer.read(path, _script_dir, _config["data_dir"])
    except ValueError as e:
        ui.msgbox(str(e), title=title)
        return False
    merge = f"Merge: bring in {plan.merge_summary()}, and keep everything else"
    replace = "Replace: make the scripts, settings, and store match the zip exactly"
    choice = ui.choose([merge, replace], prompt=f"Importing {os.path.basename(path)}.\n\n{_transfer.describe(plan)}", title=title)
    if choice is None:
        return False
    backup = _transfer.backup_path(_config["data_dir"])
    _transfer.export(backup, _script_dir, _config["data_dir"])
    _transfer.apply(plan, choice == replace, _script_dir, _config["data_dir"])
    _log(f"Imported {path} ({'replacing' if choice == replace else 'merging'}); the scripts and settings from before are in {backup}")
    native.toast("MicroKeys", f"Imported {os.path.basename(path)}. A backup of what was here is in the data folder.")
    return True


def _toggle_elevated():
    """changing the run as administrator setting"""
    on = not bool(_settings.current.get("run_elevated", False))
    _settings.update(_config, {"run_elevated": on}, _error)
    if not on and native.is_elevated():
        ui.msgbox("MicroKeys will run without administrator rights from the next time it starts.", title="MicroKeys")
    return True


_MENU_ACTIONS = {
    "net-new-token": _new_token,
    "net-enter-token": _enter_token,
    "net-off": _network_off,
    "export": _export,
    "import": _import,
    "toggle-elevated": _toggle_elevated,
}


def shutdown():
    _net.stop()
    _scheduler.stop()
    _store.save()
    for stream in (sys.stdout, sys.stderr):
        if isinstance(stream, _LogWriter):
            stream.flush()
