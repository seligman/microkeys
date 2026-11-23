# store: a dict that's kept between runs, as store.json in the data folder.
# The runtime loads it at startup and saves it after each macro if it changed.

import json
import os

from ._native import native, LOG_ERROR


class Store(dict):
    """A dict that MicroKeys saves between runs.

        from microkeys import hotkey, store

        @hotkey("ctrl+alt+c")
        def count():
            store["presses"] = store.get("presses", 0) + 1

    It's saved after every macro that changes it, including changes inside
    the lists and dicts it holds.  It keeps what JSON can: strings, numbers,
    True, False, None, and lists and dicts of those.  It's in store.json in
    the data folder (%APPDATA%\\MicroKeys).
    """

    def save(self):
        """Save now, rather than when the macro finishes."""
        save()


store = Store()
_path = None
_saved = None
_reported = None


def load(path):
    # Read store.json.  One that can't be read is moved aside, not lost.
    global _path, _saved
    _path = path
    store.clear()
    if not os.path.isfile(path):
        _saved = json.dumps({})
        return
    try:
        with open(path, "rt", encoding="utf-8-sig") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            raise ValueError("expected a JSON object")
    except Exception as e:
        bad = path + ".bad"
        try:
            os.replace(path, bad)
        except OSError:
            pass
        native.log(LOG_ERROR, f"Unable to read {path} ({e}); moved it to {bad} and started an empty store")
        data = {}
    store.update(data)
    _saved = _text()


def _text():
    return json.dumps(store, indent=4, sort_keys=True, ensure_ascii=False)


def save():
    # Write store.json if anything changed.  Something JSON can't hold is
    # reported once rather than after every macro.
    global _saved, _reported
    if _path is None:
        return
    try:
        text = _text()
    except (TypeError, ValueError) as e:
        if str(e) != _reported:
            _reported = str(e)
            native.log(LOG_ERROR, f"Unable to save the store: {e}")
        return
    _reported = None
    if text == _saved:
        return
    tmp = _path + ".tmp"
    try:
        os.makedirs(os.path.dirname(_path), exist_ok=True)
        with open(tmp, "wt", encoding="utf-8") as f:
            f.write(text + "\n")
        os.replace(tmp, _path)
        _saved = text
    except OSError as e:
        native.log(LOG_ERROR, f"Unable to save the store: {e}")
