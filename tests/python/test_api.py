import json
import os

import pytest

from microkeys import _keyspec, _runtime, _settings


def test_parse_hotkey(env):
    assert _keyspec.parse_hotkey("ctrl+alt+z") == (3, "z", "Ctrl+Alt+Z")
    assert _keyspec.parse_hotkey("Control + Shift + F5") == (5, "f5", "Ctrl+Shift+F5")
    assert _keyspec.parse_hotkey("win+e") == (8, "e", "Meta+E")
    assert _keyspec.parse_hotkey("ctrl++") == (1, "+", "Ctrl++")
    assert _keyspec.parse_hotkey("rctrl+esc") == (1, "escape", "Ctrl+Escape")
    assert _keyspec.parse_hotkey("primary+c")[0] == 1
    env.fake.platform_name = "macos"
    assert _keyspec.parse_hotkey("primary+c")[0] == 8
    for bad in ["", "ctrl+", "ctrl+alt", "a+b", "ctrl+nosuchkey", "ctrl++x"]:
        with pytest.raises(ValueError):
            _keyspec.parse_hotkey(bad)


def test_bindings_published(env):
    env.write("main.py", """
        from microkeys import hotkey, hotstring, command, windows

        @hotkey("ctrl+alt+z")
        def one(): pass

        @hotkey("f5", when="notepad.exe")
        def two(): pass

        @hotkey("f5", when=windows.match(title="*Excel*"))
        def three(): pass

        hotstring(";sig", "Best regards")

        @hotstring("btw", immediate=False, case_sensitive=True)
        def four(): pass

        command("go", one)
    """)
    assert env.start()
    hk = env.hotkeys()
    assert [row[1:] for row in hk] == [
        (3, "z", "Ctrl+Alt+Z", "", "", ""),
        (0, "f5", "F5", "notepad.exe", "", ""),
        (0, "f5", "F5", "", "*Excel*", ""),
    ]
    assert [row[1:] for row in env.hotstrings()] == [
        (";sig", True, False, "", "", ""),
        ("btw", False, True, "", "", ""),
    ]
    assert [row[1] for row in env.commands()] == ["go"]
    ids = [row[0] for row in hk + env.hotstrings() + env.commands()]
    assert len(set(ids)) == len(ids)
    assert env.fake.loaded[-1] == (True, "3 hotkeys, 2 hotstrings, 1 command")


def test_duplicate_hotkey_is_reported(env):
    env.write("a.py", """
        from microkeys import hotkey
        @hotkey("ctrl+alt+f9")
        def first(): print("first")
    """)
    env.write("b.py", """
        from microkeys import hotkey
        @hotkey("Ctrl+Alt+F9")
        def second(): print("second")
    """)
    assert env.start()
    assert len(env.hotkeys()) == 1
    assert "Ctrl+Alt+F9 at b.py:3 is already bound at a.py:3" in env.fake.log_text()
    env.press("Ctrl+Alt+F9")
    assert env.fake.logs[-1] == (1, "first")


def test_hotstring_replacement(env):
    env.write("hs.py", """
        from microkeys import hotstring, keys

        hotstring(";sig", "Best regards,\\nSam")
        hotstring("btw", "by the way", immediate=False)

        @hotstring(";up")
        def upper():
            keys.type("UP")
    """)
    assert env.start()
    env.type_trigger(";sig")
    assert env.fake.key_events() == [("down", "backspace"), ("up", "backspace")] * 4
    assert env.fake.typed() == "Best regards,\nSam"

    env.fake.sent.clear()
    env.type_trigger("btw", " ")
    assert env.fake.key_events() == [("down", "backspace"), ("up", "backspace")] * 4
    assert env.fake.typed() == "by the way "

    env.fake.sent.clear()
    env.type_trigger(";up")
    assert len(env.fake.key_events()) == 6
    assert env.fake.typed() == "UP"


def test_keep_last_good(env):
    env.write("main.py", """
        from microkeys import hotkey
        @hotkey("ctrl+alt+a")
        def a(): print("version 1")
    """)
    assert env.start()
    env.press("Ctrl+Alt+A")
    assert env.fake.logs[-1] == (1, "version 1")

    # A broken edit keeps the last good version running
    env.write("main.py", """
        from microkeys import hotkey
        @hotkey("ctrl+alt+a")
        def a(): print("version 2")
        oops(
    """)
    assert env.reload() is False
    assert "keeping the previous version" in env.fake.log_text()
    assert env.fake.loaded[-1][0] is False
    env.press("Ctrl+Alt+A")
    assert env.fake.logs[-1] == (1, "version 1")

    # An error while running the new version also keeps the old one
    env.write("main.py", """
        from microkeys import hotkey
        @hotkey("ctrl+alt+b")
        def b(): print("version 3")
        raise RuntimeError("half way")
    """)
    assert env.reload() is False
    assert [row[3] for row in env.hotkeys()] == ["Ctrl+Alt+A"]

    # Fixing it takes effect
    env.write("main.py", """
        from microkeys import hotkey
        @hotkey("ctrl+alt+b")
        def b(): print("version 4")
    """)
    assert env.reload()
    assert [row[3] for row in env.hotkeys()] == ["Ctrl+Alt+B"]

    # Deleting the file drops its bindings
    os.unlink(env.scripts / "main.py")
    assert env.reload()
    assert env.hotkeys() == []


def test_helpers_reload(env):
    env.write("_helper.py", "VALUE = 'one'\n")
    env.write("main.py", """
        from microkeys import command
        import _helper

        @command("show")
        def show(): print(_helper.VALUE)
    """)
    assert env.start()
    env.run("show")
    assert env.fake.logs[-1] == (1, "one")
    # Files starting with _ aren't scripts themselves
    assert len(_runtime._scripts) == 1
    env.write("_helper.py", "VALUE = 'two'\n")
    assert env.reload()
    env.run("show")
    assert env.fake.logs[-1] == (1, "two")


def test_bom_and_unicode(env):
    env.write("bom.py", """
        from microkeys import command, keys
        @command("go")
        def go(): keys.type("café — \U0001F600")
    """, encoding="utf-8-sig")
    assert env.start()
    env.run("go")
    assert env.fake.typed() == "café — \U0001F600"


def test_no_echo_of_top_level_expressions(env):
    env.write("echo.py", """
        import gc
        gc.collect()
        len("abc")
    """)
    assert env.start()
    assert [x for x in env.fake.logs if x[0] == 1] == []


def test_abort(env):
    env.write("long.py", """
        from microkeys import command, keys
        @command("long")
        def long(): keys.type("x" * 1000)
    """)
    assert env.start()
    env.fake.cancel_after_sends = 10
    assert env.run("long")
    assert "Stopped command 'long'" in env.fake.log_text()
    assert len(env.fake.typed()) == 10


def test_unknown_command(env):
    assert env.start()
    assert env.run("nothing") is False
    assert "No command named 'nothing'" in env.fake.log_text()


def test_binding_outside_load(env):
    from microkeys import hotkey
    with pytest.raises(RuntimeError):
        hotkey("ctrl+alt+z")


def test_starter_script(env):
    # A first run creates the folder and a starter script that loads cleanly
    assert env.start(headless=False)
    assert (env.scripts / "MicroKeys.py").is_file()
    assert env.fake.loaded[-1][0] is True
    assert len(env.hotkeys()) == 3
    assert [row[1] for row in env.hotstrings()] == [";shrug"]
    env.type_trigger(";shrug")
    assert env.fake.typed() == "¯\\_(ツ)_/¯"
    cfg = json.loads((env.scripts / "pyrightconfig.json").read_text())
    assert cfg["extraPaths"] == [str(env.lib)]
    # Settings were written with the defaults
    assert json.loads((env.data / "settings.json").read_text()) == _settings.DEFAULTS


def test_default_script_folder(env):
    assert env.start(script_dir=False)
    assert env.fake.config[0] == str(env.docs / "MicroKeys")


def test_settings(env):
    (env.data / "settings.json").write_text(json.dumps({"abort_hotkey": "ctrl+nothing", "key_delay_ms": 0}))
    (env.app / "defaults.json").write_text(json.dumps({"update_feed": "https://example.com/feed/"}))
    assert env.start()
    script_dir, mods, key, feed, check, editor = env.fake.config
    assert (mods, key) == (3, "backspace")
    assert "Bad abort_hotkey setting" in env.fake.log_text()
    assert feed == "https://example.com/feed/"
    assert _settings.current["key_delay_ms"] == 0



def test_settings_without_updater(env):
    # A build without the updater writes settings.json without its setting
    config = dict(env.config(headless=False), updates=False)
    assert _runtime.startup(config)
    fn = env.data / "settings.json"
    assert "check_updates" not in json.loads(fn.read_text())
    fn.unlink()
    _settings.add_packages(config, ["requests"], print)
    assert json.loads(fn.read_text()) == dict(_settings._first_settings(config), packages=["requests"])
    assert "check_updates" not in json.loads(fn.read_text())


def test_clip_helpers(env):
    from microkeys import clip
    assert env.start()
    env.fake.clipboard = "before"
    assert clip.copy_selection() == "selected text"
    clip.paste_text("pasted")
    assert env.fake.clipboard == "selected text"
    assert env.fake.key_events()[-4:] == [("down", "ctrl"), ("down", "v"), ("up", "v"), ("up", "ctrl")]
    env.fake.clip_changes_on_copy = False
    assert clip.copy_selection(timeout=0.05) == ""


def test_ui(env):
    from microkeys import ui
    assert env.start()
    assert ui.msgbox("Hi", buttons="yesno") == "ok"
    assert ui.input("Name?") == "typed text"
    assert ui.choose(["a", "b", "c"]) == "b"
    assert ui.choose([]) is None
    with pytest.raises(ValueError):
        ui.msgbox("x", buttons="maybe")
    ui.toast("done")
    assert env.fake.toasts[-1] == ("MicroKeys", "done")


def test_windows_helpers(env):
    from microkeys import windows
    assert env.start()
    assert [w.process for w in windows.find(title="*notepad*")] == ["notepad.exe"]
    assert windows.find(title="notepad") == []
    assert windows.find(process=lambda p: p.lower().startswith("app"))[0].title == "Calculator"
    assert windows.wait_for(title="Nope", timeout=0.3) is None
    calc = windows.find_one(title="Calculator")
    assert calc.state == "minimized"
    calc.activate()
    assert windows.active() == calc
    mons = windows.monitors()
    assert len(mons) == 2 and mons[0].primary
    calc.move_to_monitor(1, fill=True)
    assert calc.bounds == (1920, 0, 2560, 1400)
    calc.close()
    assert not calc.exists


def test_open_and_run(env):
    import microkeys
    assert env.start()
    microkeys.open("https://example.com")
    assert env.fake.opened == ["https://example.com"]
    with pytest.raises(OSError):
        microkeys.open("missing")


def test_time_sleep_is_interruptible(env):
    # time.sleep() on the script thread goes through the app, so the abort
    # hotkey can end it
    import time
    env.write("s.py", """
        import time
        from microkeys import command

        @command("nap")
        def nap():
            time.sleep(12.5)
    """)
    assert env.start()
    before = env.fake.slept
    env.run("nap")
    assert env.fake.slept - before == 12.5
    with pytest.raises(ValueError):
        time.sleep(-1)


def test_second_interrupt_while_stopping(env):
    # A second KeyboardInterrupt while handling the first still logs "Stopped"
    env.write("s.py", """
        from microkeys import command, keys

        @command("long")
        def long():
            keys.type("x" * 100)
    """)
    assert env.start()
    env.fake.cancel_after_sends = 3
    real_log = env.fake.log
    calls = {"n": 0}

    def flaky_log(level, text):
        calls["n"] += 1
        if text.startswith("Stopped") and calls.get("raised") is None:
            calls["raised"] = True
            raise KeyboardInterrupt
        real_log(level, text)

    env.fake.log = flaky_log
    try:
        assert env.run("long")
    finally:
        env.fake.log = real_log
    assert "Stopped command 'long'" in env.fake.log_text()
    assert env.fake.running == ""


def test_scripts_run_in_their_folder(env):
    # Relative paths in a script mean the script folder
    assert env.start(headless=False)
    assert os.getcwd() == str(env.scripts)
