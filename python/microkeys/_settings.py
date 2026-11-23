# MicroKeys settings: settings.json in the data folder, on top of the
# defaults.json a release ships next to the app, on top of DEFAULTS.

import json
import os

DEFAULTS = {
    # Where the scripts live.  Empty means a "MicroKeys" folder in Documents.
    "script_folder": "",
    # Stops a running macro
    "abort_hotkey": "ctrl+alt+backspace",
    # Pause between keys when typing, in milliseconds
    "key_delay_ms": 5,
    # Look for a new version once a day
    "check_updates": True,
}

current = dict(DEFAULTS)


def _read_json(fn):
    with open(fn, "rt", encoding="utf-8-sig") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError("expected a JSON object")
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
                json.dump(DEFAULTS, f, indent=4)
                f.write("\n")
        except OSError as e:
            log(f"Unable to write {fn}: {e}")

    current = dict(DEFAULTS)
    current.update(app)
    current.update(user)
    return current
