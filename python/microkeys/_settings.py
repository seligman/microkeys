# MicroKeys settings: settings.json in the data folder, on top of the
# defaults.json a release ships next to the app, on top of DEFAULTS.

import json
import os

from . import _packages

DEFAULTS = {
    # Where the scripts live.  Empty means a "MicroKeys" folder in Documents.
    "script_folder": "",
    # Stops a running macro
    "abort_hotkey": "ctrl+alt+backspace",
    # Pause between keys when typing, in milliseconds
    "key_delay_ms": 5,
    # Look for a new version once a day
    "check_updates": True,
    # How many recently copied texts clip.history() keeps; 0 turns it off
    "clipboard_history": 25,
    # What opens a file name clicked in the log, with {file} and {line} in it,
    # like "notepad++.exe -n{line} {file}".  Empty means VS Code or Notepad++
    # if one is installed, else Notepad.
    "editor": "",
    # Packages from PyPI for scripts to import; missing ones are installed
    "packages": [],
    # A port for a Python debugger such as VS Code to attach to; 0 is off
    "debug_port": 0,
}

current = dict(DEFAULTS)


def _read_json(fn):
    with open(fn, "rt", encoding="utf-8-sig") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError("expected a JSON object")
    return data


def _first_settings(config):
    # What a new settings.json holds.  Builds without the updater leave out
    # its setting.
    data = dict(DEFAULTS)
    if not config.get("updates", True):
        del data["check_updates"]
    return data


def load(config, log):
    # Read the settings, writing a default settings.json the first time so
    # there's something to edit
    global current
    app = {}
    fn = os.path.join(config["app_dir"], "defaults.json")
    if os.path.isfile(fn):
        try:
            app = _read_json(fn)
        except Exception as e:
            log(f"Ignoring {fn}: {e}")

    user = {}
    data_dir = config["data_dir"]
    fn = os.path.join(data_dir, "settings.json")
    if os.path.isfile(fn):
        try:
            user = _read_json(fn)
        except Exception as e:
            log(f"Ignoring {fn}, using the defaults: {e}")
    elif not config.get("headless"):
        try:
            os.makedirs(data_dir, exist_ok=True)
            with open(fn, "wt", encoding="utf-8") as f:
                json.dump(_first_settings(config), f, indent=4)
                f.write("\n")
        except OSError as e:
            log(f"Unable to write {fn}: {e}")

    current = dict(DEFAULTS)
    current.update(app)
    current.update(user)
    return current


def add_packages(config, specs, log):
    # Add packages to the list in settings.json, keeping everything else
    fn = os.path.join(config["data_dir"], "settings.json")
    try:
        data = _read_json(fn) if os.path.isfile(fn) else _first_settings(config)
        have = list(data.get("packages") or [])
        names = {_packages.name_of(x) for x in have}
        added = [x for x in specs if _packages.name_of(x) not in names]
        if not added:
            return
        data["packages"] = have + added
        tmp = fn + ".tmp"
        with open(tmp, "wt", encoding="utf-8") as f:
            json.dump(data, f, indent=4)
            f.write("\n")
        os.replace(tmp, fn)
    except Exception as e:
        log(f"Unable to add {', '.join(specs)} to the packages setting in {fn}: {e}")
