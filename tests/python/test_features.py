# The 1.1 features: bindings made by macros, timers, window and clipboard
# events, rich clipboard data, the store, the console, packages, and
# messages on screen.

import datetime
import json
import os

import pytest

from microkeys import _bindings, _packages, _runtime, _settings, _store, _timers, clip, ui

FIRE_HOTKEY, FIRE_COMMAND, FIRE_WINDOW, FIRE_TIMER, FIRE_CLIPBOARD = 1, 3, 4, 5, 6


def test_macro_binds_and_unbinds(env):
    env.write("modal.py", """
        from microkeys import hotkey, command, unbind, keys

        @hotkey("f1")
        def help(): print("help")

        @command("enter-mode")
        def enter():
            hotkey("j", lambda: print("down"))
            hotkey("f1", lambda: print("mode help"))

        @command("leave-mode")
        def leave():
            print("removed", unbind(hotkey="j"))
    """)
    assert env.start()
    f1_id = env.hotkey_id("F1")
    env.run("enter-mode")
    assert sorted(row[3] for row in env.hotkeys()) == ["F1", "J"]
    # The macro's F1 wins over the script's, without a complaint
    env.press("F1")
    assert env.fake.logs[-1] == (1, "mode help")
    assert "already bound" not in env.fake.log_text()
    env.run("leave-mode")
    assert env.fake.logs[-1] == (1, "removed 1")
    assert [row[3] for row in env.hotkeys()] == ["F1"]
    env.run("enter-mode")
    # Reloading drops what macros bound, and the script's F1 is back
    assert env.reload()
    assert [row[3] for row in env.hotkeys()] == ["F1"]
    env.press("F1")
    assert env.fake.logs[-1] == (1, "help")
    assert env.hotkey_id("F1") != f1_id


def test_ids_survive_other_changes(env):
    env.write("ids.py", """
        from microkeys import hotkey, command

        @hotkey("ctrl+alt+a")
        def a(): pass

        @command("more")
        def more():
            hotkey("ctrl+alt+b", a)
    """)
    assert env.start()
    before = env.hotkey_id("Ctrl+Alt+A")
    env.run("more")
    assert env.hotkey_id("Ctrl+Alt+A") == before
    # A job for a binding that's gone does nothing
    stale = env.hotkey_id("Ctrl+Alt+B")
    assert env.reload()
    assert _runtime.fire(FIRE_HOTKEY, stale, "") is True


def test_unbind_kinds(env):
    env.write("u.py", """
        from microkeys import hotkey, hotstring, command, every, unbind

        def shared(): pass
        hotkey("ctrl+alt+a", shared)
        hotkey("ctrl+alt+b", shared)
        every(60, shared)
        hotstring(";x", "replacement")
        command("c", lambda: None)
        print("in load", unbind(command="c"))

        @command("drop")
        def drop():
            print(unbind(shared), unbind(hotstring=";X"), unbind(hotkey="ctrl+alt+z"))
    """)
    assert env.start()
    assert "in load 1" in env.fake.log_text()
    assert [row[1] for row in env.commands()] == ["drop"]
    env.run("drop")
    assert env.fake.logs[-1] == (1, "3 1 0")
    assert env.hotkeys() == [] and env.hotstrings() == []
    with pytest.raises(TypeError):
        _bindings.unbind()


def test_on_start_and_on_reload(env):
    env.write("events.py", """
        from microkeys import on_start, on_reload

        @on_start
        def started(): print("started")

        @on_reload()
        def reloaded(): print("reloaded")
    """)
    assert env.start()
    out = [text for level, text in env.fake.logs if level == 1]
    assert out == ["started"]
    assert env.reload()
    out = [text for level, text in env.fake.logs if level == 1]
    assert out == ["started", "reloaded"]


def test_handler_signatures_checked_at_load(env):
    env.write("bad.py", """
        from microkeys import windows

        @windows.on_open
        def no_window_argument(): pass
    """)
    assert env.start() is False
    assert "should take one argument, the window" in env.fake.log_text()


def test_window_events(env):
    env.write("win.py", """
        from microkeys import windows

        @windows.on_activate(when="notepad.exe")
        def front(win): print("front", win.title)

        @windows.on_open
        def opened(win): print("opened", win.process)
    """)
    assert env.start()
    events = env.window_events()
    assert [row[1:] for row in events] == [(1, "notepad.exe", "", ""), (2, "", "", "")]
    _runtime.fire(FIRE_WINDOW, events[0][0], str(0x101))
    assert env.fake.logs[-1] == (1, "front Untitled - Notepad")
    _runtime.fire(FIRE_WINDOW, events[1][0], str(0x202))
    assert env.fake.logs[-1] == (1, "opened Code.exe")
    # A window that closed before its turn is skipped
    count = len(env.fake.logs)
    assert _runtime.fire(FIRE_WINDOW, events[1][0], str(0x999))
    assert len(env.fake.logs) == count


def test_timer_registration(env):
    env.write("t.py", """
        import datetime
        from microkeys import every, after, at

        @every(minutes=5)
        def tick(): print("tick")

        after(2, lambda: print("later"))
        at("9:30", tick)
        at(datetime.time(21, 5), tick)
    """)
    assert env.start()
    assert "1 command" not in env.fake.loaded[-1][1]
    assert "4 timers" in env.fake.loaded[-1][1]
    timers = [entry for kind, entry, c in _runtime._actions.values() if kind == FIRE_TIMER]
    assert sorted(t.kind for t in timers) == ["after", "at", "at", "every"]
    later = next(t for t in timers if t.kind == "after")
    # A one-time timer is gone once it has run
    assert _runtime.fire(FIRE_TIMER, later.id, "")
    assert env.fake.logs[-1] == (1, "later")
    assert "after" not in [entry.kind for kind, entry, c in _runtime._actions.values() if kind == FIRE_TIMER]
    for bad in ["25:00", "9", "noon"]:
        with pytest.raises(ValueError):
            _timers.parse_time(bad)
    with pytest.raises(ValueError):
        _timers.every(0)


def test_scheduler(env):
    sched = _timers.Scheduler()
    every = _bindings.TimerEntry("every", 10, None, print, "x", 0)
    once = _bindings.TimerEntry("after", 5, None, print, "x", 100.0)
    daily = _bindings.TimerEntry("at", 0, datetime.time(9, 0), print, "x", 0)
    for i, entry in enumerate([every, once, daily], start=1):
        entry.id = i
    sched.timers = {1: [every, 50.0], 2: [once, 105.0], 3: [daily, 1000.0]}

    # Nothing due yet: wait for the soonest
    assert sched.due(40.0, 900.0) == 10.0
    assert env.fake.posts == []
    # The every() timer is due; it's handed over and comes round again
    sched.due(50.0, 910.0)
    assert env.fake.posts == [(FIRE_TIMER, 1, "")]
    assert sched.timers[1][1] == 60.0
    # Still waiting to run, so the next one doesn't pile up behind it
    sched.due(60.0, 920.0)
    assert len(env.fake.posts) == 1
    sched.ran(1)
    # Far behind (the computer slept): skip ahead rather than catch up
    sched.due(500.0, 930.0)
    assert sched.timers[1][1] == 510.0
    assert len(env.fake.posts) == 3
    assert once.spent and 2 not in sched.timers
    # A daily time that's long past is skipped until tomorrow
    sched.due(510.0, 1000.0 + 3600)
    assert (FIRE_TIMER, 3, "") not in env.fake.posts
    # Paused: nothing is handed over
    env.fake.is_paused = True
    sched.ran(1)
    count = len(env.fake.posts)
    sched.due(600.0, 2000.0)
    assert len(env.fake.posts) == count


def test_one_time_timers_survive_a_publish(env):
    # Two timers handed over together: the first one running (and so
    # changing the bindings) must not lose the second
    env.write("two.py", """
        from microkeys import after

        after(1, lambda: print("A"))
        after(1, lambda: print("B"))
    """)
    assert env.start()
    timers = sorted((entry for kind, entry, c in _runtime._actions.values() if kind == FIRE_TIMER), key=lambda t: t.id)
    sched = _timers.Scheduler()
    sched.timers = {t.id: [t, 0.0] for t in timers}
    sched.due(10.0, 10.0)
    assert [kind for kind, id, extra in env.fake.posts] == [FIRE_TIMER, FIRE_TIMER]
    for kind, id, extra in list(env.fake.posts):
        _runtime.fire(kind, id, extra)
    assert [text for level, text in env.fake.logs if level == 1] == ["A", "B"]


def test_one_time_timers_wait(env):
    # Paused, or with too much waiting to run, a one-time timer is held
    # rather than lost; a repeating one just misses that run
    once = _bindings.TimerEntry("after", 5, None, print, "x", 0.0)
    every = _bindings.TimerEntry("every", 10, None, print, "x", 0)
    once.id, every.id = 1, 2
    sched = _timers.Scheduler()
    sched.timers = {1: [once, 5.0], 2: [every, 10.0]}
    env.fake.is_paused = True
    sched.due(20.0, 20.0)
    assert env.fake.posts == [] and not once.spent and 1 in sched.timers
    env.fake.is_paused = False
    env.fake.queue_full = True
    sched.due(25.0, 25.0)
    assert not once.spent and 2 not in sched.waiting
    env.fake.queue_full = False
    sched.due(30.0, 30.0)
    assert once.spent and (FIRE_TIMER, 1, "") in env.fake.posts and 2 in sched.waiting


def test_dropped_timer_comes_round_again(env):
    env.write("t.py", """
        from microkeys import every

        @every(10)
        def tick(): print("tick")
    """)
    assert env.start()
    timer = next(entry for kind, entry, c in _runtime._actions.values() if kind == FIRE_TIMER)
    _runtime._scheduler.waiting.add(timer.id)
    assert _runtime.fire(FIRE_TIMER, timer.id, "dropped")
    assert timer.id not in _runtime._scheduler.waiting
    assert "tick" not in env.fake.log_text()


def test_macro_hotkey_wins_over_filtered(env):
    env.write("m.py", """
        from microkeys import hotkey, command

        @hotkey("escape", when="excel.exe")
        def in_excel(): print("excel")

        @command("mode")
        def mode():
            hotkey("escape", lambda: print("mode off"))
    """)
    assert env.start()
    env.run("mode")
    assert [(row[3], row[4]) for row in env.hotkeys()] == [("Escape", "")]


def test_console_keeps_microkeys_names(env):
    env.write("clip.py", """
        VALUE = 1
    """)
    assert env.start()
    _runtime.console("clip.get.__module__")
    assert env.fake.logs[-1] == (1, "'microkeys.clip'")
    count = len(env.fake.logs)
    _runtime.console("")
    assert len(env.fake.logs) == count


def test_next_daily():
    noon = datetime.datetime(2026, 3, 1, 12, 0).timestamp()
    assert _timers.next_daily(datetime.time(13, 0), noon) == datetime.datetime(2026, 3, 1, 13, 0).timestamp()
    assert _timers.next_daily(datetime.time(9, 0), noon) == datetime.datetime(2026, 3, 2, 9, 0).timestamp()


def test_rich_clipboard(env):
    assert env.start()
    clip.set(html="<p>Hello <b>there</b> &amp; you</p><p>Two</p>")
    assert clip.formats() == ["text", "html"]
    assert clip.get() == "Hello there & you\nTwo"
    assert clip.get_html().startswith("<p>Hello")
    png = b"\x89PNG\r\n\x1a\n" + b"rest"
    clip.set("caption", image=png, files=["a.txt"])
    assert clip.get_image() == png
    assert clip.get_files() == [os.path.abspath("a.txt")]
    assert clip.get_rtf() is None
    with pytest.raises(ValueError):
        clip.set(image=b"GIF89a")
    with pytest.raises(ValueError):
        clip.set()
    saved = clip.save()
    clip.set("something else")
    assert clip.restore(saved)
    assert clip.get() == "caption" and clip.get_image() == png
    # paste_text puts back everything, not just the text
    clip.paste_text("pasted")
    assert clip.get_image() == png


def test_clipboard_history_and_events(env):
    env.write("c.py", """
        from microkeys import clip

        @clip.on_change
        def changed(): print("changed", clip.get())
    """)
    assert env.start()
    for text in ["one", "two", "one", "   "]:
        env.fake.clip_set(text)
        _runtime.fire(FIRE_CLIPBOARD, 0, "")
    assert clip.history() == ["one", "two"]
    assert env.fake.logs[-1] == (1, "changed    ")
    env.fake.concealed = True
    env.fake.clip_set("password")
    _runtime.fire(FIRE_CLIPBOARD, 0, "")
    assert "password" not in clip.history()
    # Paused: the history still keeps up, the handlers don't run
    env.fake.concealed = False
    env.fake.is_paused = True
    env.fake.clip_set("three")
    count = len(env.fake.logs)
    _runtime.fire(FIRE_CLIPBOARD, 0, "")
    assert clip.history()[0] == "three" and len(env.fake.logs) == count
    _settings.current["clipboard_history"] = 2
    env.fake.clip_set("four")
    _runtime.fire(FIRE_CLIPBOARD, 0, "")
    assert clip.history() == ["four", "three"]


def test_store(env):
    env.write("s.py", """
        from microkeys import command, store

        @command("count")
        def count():
            store["n"] = store.get("n", 0) + 1
            store.setdefault("seen", []).append(store["n"])

        @command("bad")
        def bad():
            store["oops"] = object()
    """)
    assert env.start()
    env.run("count")
    env.run("count")
    path = env.data / "store.json"
    assert json.loads(path.read_text()) == {"n": 2, "seen": [1, 2]}
    env.run("bad")
    env.run("bad")
    assert env.fake.log_text().count("Unable to save the store") == 1
    del _store.store["oops"]
    # A new run picks up where the last left off
    _store.load(str(path))
    assert _store.store["n"] == 2
    path.write_text("{not json")
    _store.load(str(path))
    assert _store.store == {} and (env.data / "store.json.bad").is_file()


def test_console(env):
    env.write("tools.py", """
        def double(x): return x * 2
    """)
    assert env.start()
    _runtime.console("1 + 1")
    assert env.fake.logs[-2:] == [(0, ">>> 1 + 1"), (1, "2")]
    # Scripts and the package are at hand
    _runtime.console("tools.double(21), keys.__name__")
    assert env.fake.logs[-1] == (1, "(42, 'microkeys.keys')")
    _runtime.console("for i in range(2):")
    assert env.fake.console_more[-1] is True
    _runtime.console("    print(i)")
    _runtime.console("")
    assert [text for level, text in env.fake.logs[-2:]] == ["0", "1"]
    assert env.fake.console_more[-1] is False
    _runtime.console("1 / 0")
    assert "ZeroDivisionError" in env.fake.logs[-1][1]
    assert env.fake.toasts == []
    _runtime.console("x = (")
    _runtime.console("]")
    assert "SyntaxError" in env.fake.log_text()


def test_messages_on_screen(env):
    env.write("progress.py", """
        from microkeys import command, ui

        @command("work")
        def work():
            ui.show("Step 1")
            ui.show(2)

        @command("saved")
        def saved():
            ui.show("Saved", seconds=5)

        @command("hidden")
        def hidden():
            ui.show("Working")
            ui.hide()

        @command("forever")
        def forever():
            ui.show("Caps mode", seconds=float("inf"))

        @command("bad")
        def bad():
            ui.show("Working")
            ui.show("x", seconds=0)
    """)
    assert env.start()
    # Up while the macro runs, then for two more seconds
    env.run("work")
    assert env.fake.overlays == [("Step 1", -1), ("2", -1), ("2", 2000)]
    del env.fake.overlays[:]
    env.run("saved")
    env.run("hidden")
    env.run("forever")
    assert env.fake.overlays == [("Saved", 5000), ("Working", -1), ("", 0), ("Caps mode", 0x7FFFFFFF)]
    del env.fake.overlays[:]
    # A macro that fails still lets its message go
    env.run("bad")
    assert "show() needs seconds greater than 0" in env.fake.log_text()
    assert env.fake.overlays == [("Working", -1), ("Working", 2000)]
    del env.fake.overlays[:]
    ui.show("")
    assert env.fake.overlays == [("", 0)]


def test_package_names(env):
    assert _packages.name_of("Requests>=2.0") == "requests"
    assert _packages.name_of("zope.interface") == "zope-interface"
    assert _packages.name_of(">=2") is None


def test_packages_installed_when_missing(env, monkeypatch):
    calls = []
    monkeypatch.setattr(_packages, "installed", lambda local: {"requests"})
    monkeypatch.setattr(_packages, "install", lambda config, specs, upgrade=False: calls.append(specs) or False)
    (env.data / "settings.json").write_text(json.dumps({"packages": ["requests", "PyYAML>=6"]}))
    assert env.start(headless=False)
    assert calls == [["PyYAML>=6"]]
    assert "Unable to install PyYAML>=6" in env.fake.log_text()
    # A failure isn't retried on every reload
    assert env.reload()
    assert len(calls) == 1


def test_add_packages_setting(env):
    (env.data / "settings.json").write_text(json.dumps({"key_delay_ms": 9, "packages": ["requests"]}))
    _settings.add_packages(env.config(), ["Requests", "pyyaml"], print)
    assert json.loads((env.data / "settings.json").read_text()) == {"key_delay_ms": 9, "packages": ["requests", "pyyaml"]}


def test_stub_copy(env, tmp_path):
    lib = tmp_path / "lib"
    (lib / "microkeys").mkdir(parents=True)
    (lib / "pyproject.toml").write_text("[build-system]\n")
    (lib / "VERSION").write_text("9.9.9\n")
    (lib / "microkeys" / "__init__.py").write_text("one")
    target = tmp_path / "stubs"
    (target / "microkeys").mkdir(parents=True)
    (target / "microkeys" / "gone.py").write_text("old")
    _runtime._sync_stubs(str(lib), str(target))
    assert (target / "microkeys" / "__init__.py").read_text() == "one"
    assert (target / "VERSION").read_text() == "9.9.9\n"
    assert not (target / "microkeys" / "gone.py").exists()
