import os
import sys
import textwrap
import time

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "python"))
sys.path.insert(0, HERE)

from fake_microkeys import FakeMicrokeys

FAKE = FakeMicrokeys()
sys.modules["_microkeys"] = FAKE

import microkeys
from microkeys import _runtime, _settings, _bindings, _packages, _store, _timers, clip, ui


class Env:
    # A script folder, data folder and app folder in a temp dir, plus helpers
    # to load scripts and fire their bindings the way the app would
    def __init__(self, fake, root):
        self.fake = fake
        self.root = root
        self.scripts = root / "scripts"
        self.data = root / "data"
        self.app = root / "app"
        self.lib = self.app / "lib"
        self.docs = root / "docs"
        self.local = root / "local"
        for d in (self.data, self.app, self.lib, self.docs):
            d.mkdir(parents=True, exist_ok=True)

    def write(self, name, text, encoding="utf-8"):
        self.scripts.mkdir(parents=True, exist_ok=True)
        (self.scripts / name).write_bytes(textwrap.dedent(text).encode(encoding))

    def config(self, headless=True, script_dir=True):
        return {
            "app_dir": str(self.app),
            "lib_dir": str(self.lib),
            "data_dir": str(self.data),
            "documents_dir": str(self.docs),
            "local_dir": str(self.local),
            "script_dir": str(self.scripts) if script_dir else "",
            "headless": headless,
        }

    def start(self, headless=True, script_dir=True):
        return _runtime.startup(self.config(headless, script_dir))

    def reload(self):
        return _runtime.reload()

    def hotkeys(self):
        return self.fake.bindings[0]

    def hotstrings(self):
        return self.fake.bindings[1]

    def commands(self):
        return self.fake.bindings[2]

    def window_events(self):
        return self.fake.bindings[3]

    def hotkey_id(self, description):
        for row in self.hotkeys():
            if row[3] == description:
                return row[0]
        raise KeyError(description)

    def press(self, description):
        return _runtime.fire(1, self.hotkey_id(description), "")

    def type_trigger(self, trigger, end=""):
        for row in self.hotstrings():
            if row[1] == trigger:
                return _runtime.fire(2, row[0], end)
        raise KeyError(trigger)

    def run(self, name):
        return _runtime.run_command(name)


@pytest.fixture
def env(tmp_path):
    FAKE.reset()
    _runtime._scripts.clear()
    _runtime._actions.clear()
    _runtime._commands.clear()
    _runtime._config.clear()
    _runtime._script_dir = None
    _runtime._published = None
    _runtime._console_lines.clear()
    _runtime._console_ns.clear()
    _runtime._scheduler = _timers.Scheduler()
    _settings.current = dict(_settings.DEFAULTS)
    _bindings.end()
    _bindings.live = None
    _bindings.loaded.clear()
    _bindings.changed = False
    _packages._failed.clear()
    _store.store.clear()
    _store._path = None
    clip.clear_history()
    ui._lasting = None
    saved = (sys.stdout, sys.stderr, list(sys.path), time.sleep, os.getcwd())
    yield Env(FAKE, tmp_path)
    _runtime._scheduler.stop()
    os.chdir(saved[4])
    sys.stdout, sys.stderr = saved[0], saved[1]
    time.sleep = saved[3]
    sys.path[:] = saved[2]
    for name in [n for n, m in sys.modules.items() if getattr(m, "__file__", "") and str(tmp_path) in str(getattr(m, "__file__", ""))]:
        del sys.modules[name]
