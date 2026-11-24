# The 1.2 features: remapped keys, mouse hotkeys, the screen, controls in
# windows, file dialogs, export and import, messages between machines, and
# running as administrator.

import json
import os
import socket
import time
import zipfile

import pytest

from microkeys import _net, _runtime, _settings, _store, _transfer, controls, keys, net, screen, ui, windows

FIRE_HOTKEY, FIRE_MESSAGE = 1, 7


def remaps(env):
    return env.fake.bindings[4]


def test_remaps(env):
    env.write("remap.py", """
        from microkeys import keys, command, unbind

        keys.remap("capslock", "ctrl")
        keys.remap("Insert", None)
        keys.remap("rctrl", "apps", when="code.exe")

        @command("swap")
        def swap():
            keys.remap("f1", "escape")
            unbind(remap="insert")
    """)
    assert env.start()
    assert sorted(r[1:] for r in remaps(env)) == [
        ("capslock", "ctrl", "", "", ""),
        ("insert", "", "", "", ""),
        ("rctrl", "apps", "code.exe", "", ""),
    ]
    assert "3 remaps" in env.fake.loaded[-1][1]
    env.run("swap")
    assert sorted(r[1] for r in remaps(env)) == ["capslock", "f1", "rctrl"]


def test_remap_mistakes(env):
    for bad, message in ((("ctrl+c", "x"), "one key"), (("mousex1", "a"), "keyboard keys"), (("a", "mouseleft"), "keyboard keys"),
                         (("nosuchkey", "a"), "Unknown key")):
        env.write("bad.py", f"""
            from microkeys import keys
            keys.remap{bad!r}
        """)
        assert not env.start()
        assert message in env.fake.log_text(), bad
        env.fake.logs.clear()


def test_remap_clash(env):
    env.write("a.py", "from microkeys import keys\nkeys.remap('f1', 'escape')\n")
    env.write("b.py", "from microkeys import keys\nkeys.remap('f1', 'tab')\n")
    assert env.start()
    assert [r[2] for r in remaps(env)] == ["escape"]
    assert "Remap of f1 at b.py:2 is already bound at a.py:2" in env.fake.log_text()


def test_mouse_hotkeys(env):
    env.write("mouse.py", """
        from microkeys import hotkey

        @hotkey("ctrl+mousex1")
        def back(): print("back")

        @hotkey("Alt+WheelUp")
        def louder(): print("louder")
    """)
    assert env.start()
    assert sorted((row[1], row[2], row[3]) for row in env.hotkeys()) == [(1, "mousex1", "Ctrl+Mousex1"), (2, "wheelup", "Alt+Wheelup")]
    env.press("Ctrl+Mousex1")
    assert env.fake.logs[-1] == (1, "back")
    # Mouse buttons can't be sent as keys
    with pytest.raises(ValueError):
        keys.press("mousex1")


def test_left_button_needs_a_modifier(env):
    env.write("left.py", "from microkeys import hotkey\nhotkey('mouseleft', print)\n")
    assert not env.start()
    assert "needs a modifier" in env.fake.log_text()


def small_monitors(monkeypatch, fake):
    # Two little monitors, so captures of "everything" stay quick
    mons = [{"x": 0, "y": 0, "width": 64, "height": 48, "work_x": 0, "work_y": 0, "work_width": 64, "work_height": 44, "primary": True, "dpi": 96},
            {"x": 64, "y": 0, "width": 32, "height": 40, "work_x": 64, "work_y": 0, "work_width": 32, "work_height": 40, "primary": False, "dpi": 96}]
    monkeypatch.setattr(fake, "monitors", lambda: mons)


def test_screen_capture_and_find(env, tmp_path, monkeypatch):
    fake = env.fake
    small_monitors(monkeypatch, fake)
    fake.paint(10, 5, 6, 4, (255, 0, 0))
    fake.paint(12, 6, 2, 2, (0, 0, 255))
    shot = screen.capture((8, 4, 12, 8))
    assert (shot.width, shot.height, shot.x, shot.y) == (12, 8, 8, 4)
    assert shot.pixel(2, 1) == (255, 0, 0)
    assert screen.pixel(12, 6) == (0, 0, 255)
    assert screen.pixel(0, 0) == (64, 64, 64)

    # Saved and loaded again, it's found where it came from
    needle = shot.crop(2, 1, 6, 4)
    assert (needle.x, needle.y) == (10, 5)
    path = tmp_path / "needle.png"
    needle.save(path)
    found = screen.find(str(path))
    assert found == (10, 5, 6, 4) and found.center == (13, 7)
    assert screen.find(path, region=(30, 0, 30, 30)) is None
    assert screen.find_all(screen.load(path.read_bytes())) == [found]

    # Close colours match within the tolerance
    fake.paint(40, 30, 6, 4, (250, 4, 4))
    fake.paint(42, 31, 2, 2, (0, 0, 250))
    assert len(screen.find_all(needle)) == 1
    assert len(screen.find_all(needle, tolerance=8)) == 2

    # The whole screen is every monitor
    screen.capture()
    assert fake.captures[-1] == (0, 0, 96, 48)
    screen.capture(windows.find_one(process="notepad.exe"))
    assert fake.captures[-1] == (100, 100, 800, 600)
    screen.capture(windows.monitors()[1])
    assert fake.captures[-1] == (64, 0, 32, 40)


def test_screen_waits(env):
    fake = env.fake
    needle = screen.Image(2, 2, bytes((0, 255, 0, 255)) * 4)
    assert screen.wait_for(needle, timeout=0.5, region=(0, 0, 20, 20)) is None
    assert fake.slept >= 0.5
    fake.paint(3, 3, 2, 2, (0, 255, 0))
    assert screen.wait_for(needle, timeout=1, region=(0, 0, 20, 20)) == (3, 3, 2, 2)
    assert screen.wait_for_color(3, 3, "#00ff00")
    assert screen.wait_for_color(3, 3, (0, 250, 0), tolerance=5)
    assert not screen.wait_for_color(3, 3, (0, 250, 0), timeout=0.2)
    with pytest.raises(ValueError):
        screen.wait_for_color(0, 0, "green")


def test_offline_png():
    from microkeys import _offline
    pixels = bytes([1, 2, 3, 255, 4, 5, 6, 128, 7, 8, 9, 0, 10, 11, 12, 255, 13, 14, 15, 255, 16, 17, 18, 255])
    assert _offline.image_decode(_offline.image_encode(3, 2, pixels)) == (3, 2, pixels)
    with pytest.raises(ValueError):
        _offline.image_decode(b"GIF89a")


def build_form(fake):
    # A window with a text box, a check box, and a button, in the fake
    fake.add_control(1, role="pane", name="Form", window=0x101)
    fake.add_control(2, parent=1, role="edit", name="Name:", automation_id="NameBox", has_value=True, read_only=False,
                     value="", actions=["set_value"], bounds=(10, 10, 100, 20))
    fake.add_control(3, parent=1, role="checkbox", name="I agree", automation_id="Agree", toggle=0, actions=["toggle"])
    fake.add_control(4, parent=1, role="button", name="Go", actions=["click"], bounds=(10, 40, 50, 20))
    fake.add_control(5, parent=1, role="text", name="Status", bounds=(70, 40, 50, 20))


def test_controls(env):
    fake = env.fake
    build_form(fake)
    box = controls.find(automation_id="NameBox")
    assert box.role == "edit" and box.name == "Name:"
    box.value = "Ada"
    assert box.value == "Ada" and box.text == "Ada"
    check = controls.find(name="i agree", role="checkbox")
    assert check.checked is False
    check.click()
    assert check.checked is True
    go = controls.find(name="G?", role="button")
    go.click()
    assert fake.control_actions[-1] == (4, "click", "")
    assert [c.name for c in controls.find_all(window=windows.find_one(process="notepad.exe"))] == ["Form", "Name:", "I agree", "Go", "Status"]
    form = controls.find(name="Form")
    assert [c.role for c in form.children] == ["edit", "checkbox", "button", "text"]
    assert go.parent == form
    assert form.find(role="text").name == "Status"
    assert controls.at(75, 45).name == "Status"
    assert box.window.process == "notepad.exe"
    assert box.center == (60, 20)

    # A control that can't be clicked through UI Automation gets a mouse click
    status = controls.find(name="Status")
    status.click()
    assert fake.mouse_events[-3:] == [("move", 95, 50), ("left", "down"), ("left", "up")]
    with pytest.raises(ValueError):
        status.set_value("x")
    with pytest.raises(ValueError):
        controls.find(role="widget")
    assert controls.find(name="Nothing") is None
    assert controls.wait_for(name="Nothing", timeout=0.4) is None

    # Controls let go of their handles
    handle = box._handle
    del box
    assert handle in fake.released


def test_choose_file(env):
    fake = env.fake
    fake.file_answer = r"C:\out\report.csv"
    assert ui.choose_file(save=True, types=[("CSV files", "*.csv")], default="report.csv") == r"C:\out\report.csv"
    assert fake.file_dialogs[-1] == (True, "Save as", [("CSV files", "*.csv")], "report.csv")
    fake.file_answer = None
    assert ui.choose_file() is None
    assert fake.file_dialogs[-1] == (False, "Open", [], "")


def test_export_and_import(env, tmp_path):
    env.write("main.py", "print('main v1')\n")
    env.write("_helper.py", "X = 1\n")
    (env.scripts / "sub").mkdir()
    (env.scripts / "sub" / "data.txt").write_text("notes")
    (env.scripts / "__pycache__").mkdir()
    (env.scripts / "__pycache__" / "x.pyc").write_bytes(b"junk")
    (env.data / "settings.json").write_text(json.dumps({"key_delay_ms": 9, "script_folder": "C:\\mine", "packages": ["requests"]}))
    assert env.start()
    _store.store["count"] = 3
    zip_path = str(tmp_path / "export.zip")
    assert _transfer.export(zip_path, str(env.scripts), str(env.data)) == 3
    with zipfile.ZipFile(zip_path) as zf:
        names = sorted(zf.namelist())
        settings = json.loads(zf.read("settings.json"))
    assert names == ["microkeys-export.json", "scripts/_helper.py", "scripts/main.py", "scripts/sub/data.txt", "settings.json", "store.json"]
    # The script folder belongs to this machine
    assert "script_folder" not in settings

    # Change things here, then see what importing would do
    env.write("main.py", "print('main v2')\n")
    env.write("extra.py", "print('extra')\n")
    os.unlink(env.scripts / "_helper.py")
    (env.data / "settings.json").write_text(json.dumps({"key_delay_ms": 1, "editor": "x", "script_folder": "C:\\mine",
                                                        "packages": ["rich"]}))
    plan = _transfer.read(zip_path, str(env.scripts), str(env.data))
    assert plan.added == ["_helper.py"] and plan.changed == ["main.py"] and plan.removed == ["extra.py"]
    assert plan.settings_changed == ["key_delay_ms", "packages"] and plan.settings_removed == ["editor"]
    assert "Changed files: main.py" in _transfer.describe(plan)

    # Merging keeps what's only here, and combines lists
    _store.store.clear()
    _store.store["other"] = 1
    _transfer.apply(plan, False, str(env.scripts), str(env.data))
    assert (env.scripts / "main.py").read_text() == "print('main v1')\n"
    assert (env.scripts / "extra.py").exists() and (env.scripts / "_helper.py").exists()
    merged = json.loads((env.data / "settings.json").read_text())
    assert merged == {"key_delay_ms": 9, "editor": "x", "script_folder": "C:\\mine", "packages": ["rich", "requests"]}
    assert dict(_store.store) == {"other": 1, "count": 3}

    # Replacing makes it match, but keeps this machine's script folder
    _transfer.apply(_transfer.read(zip_path, str(env.scripts), str(env.data)), True, str(env.scripts), str(env.data))
    assert not (env.scripts / "extra.py").exists()
    assert json.loads((env.data / "settings.json").read_text()) == {"key_delay_ms": 9, "packages": ["requests"], "script_folder": "C:\\mine"}
    assert dict(_store.store) == {"count": 3}


def test_import_rejects_bad_zips(env, tmp_path):
    assert env.start()
    bad = tmp_path / "bad.zip"
    bad.write_bytes(b"not a zip")
    with pytest.raises(ValueError, match="isn't a zip"):
        _transfer.read(str(bad), str(env.scripts), str(env.data))
    with zipfile.ZipFile(bad, "w") as zf:
        zf.writestr("scripts/x.py", "")
    with pytest.raises(ValueError, match="isn't a MicroKeys export"):
        _transfer.read(str(bad), str(env.scripts), str(env.data))
    with zipfile.ZipFile(bad, "w") as zf:
        zf.writestr("microkeys-export.json", json.dumps({"format": 1}))
        zf.writestr("scripts/../../evil.py", "")
    with pytest.raises(ValueError, match="outside the script folder"):
        _transfer.read(str(bad), str(env.scripts), str(env.data))


def test_import_from_the_menu(env, tmp_path):
    env.write("main.py", "print('v1')\n")
    assert env.start(headless=False)
    zip_path = str(tmp_path / "mine.zip")
    env.fake.file_answer = zip_path
    assert _runtime.action("export")
    assert os.path.isfile(zip_path)
    assert env.fake.file_dialogs[-1][0] is True
    env.write("main.py", "print('v2')\n")
    env.fake.answers["ask_choice"] = 0
    assert _runtime.action("import")
    prompt = env.fake.dialogs[-1][2]
    assert "Changed files: main.py" in prompt
    assert (env.scripts / "main.py").read_text() == "print('v1')\n"
    backups = os.listdir(env.data / "backups")
    assert len(backups) == 1 and backups[0].startswith("before-import-")
    with zipfile.ZipFile(env.data / "backups" / backups[0]) as zf:
        assert zf.read("scripts/main.py") == b"print('v2')\n"


def test_tokens():
    token = _net.new_token()
    assert token.startswith("mknet1-") and len(token) == 37
    assert _net.check_token(" " + token.lower() + "\n") == token
    with pytest.raises(ValueError, match="starts with"):
        _net.check_token("abc")
    with pytest.raises(ValueError, match="30 characters"):
        _net.check_token(token[:-1])
    with pytest.raises(ValueError, match="perhaps O"):
        _net.check_token(token[:10] + "0" + token[11:])
    typo = token[:10] + ("A" if token[10] != "A" else "B") + token[11:]
    with pytest.raises(ValueError, match="typo"):
        _net.check_token(typo)


def test_message_checks():
    token = _net.new_token()
    checker = _net.Checker(token)
    line = _net.pack(token, "laptop", "desktop", "lock", [1, "two"], {"three": 3}).decode()
    msg = checker.open(line)
    assert (msg["from"], msg["name"], msg["args"], msg["kwargs"]) == ("laptop", "lock", [1, "two"], {"three": 3})
    with pytest.raises(ValueError, match="copy"):
        checker.open(line)
    with pytest.raises(ValueError, match="signature"):
        _net.Checker(_net.new_token()).open(line)
    old = _net.pack(token, "laptop", "desktop", "lock", [], {}, now=time.time() - 600).decode()
    with pytest.raises(ValueError, match="clock"):
        checker.open(old)
    tampered = json.loads(line)
    tampered["msg"] = tampered["msg"].replace('"lock"', '"wipe"')
    with pytest.raises(ValueError, match="signature"):
        _net.Checker(token).open(json.dumps(tampered))
    with pytest.raises(TypeError, match="JSON"):
        _net.pack(token, "a", "b", "c", [object()], {})


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def test_messages_between_machines(env):
    port = free_port()
    (env.data / "settings.json").write_text(json.dumps({"net_name": "desk", "net_port": port, "net_listen": "127.0.0.1"}))
    env.write("net.py", """
        from microkeys import net, log

        @net.on("greet")
        def greet(name, punct="!"):
            log("hello " + name + punct + " from " + net.sender())
    """)
    token = _net.new_token()
    _net.save_token(str(env.data), token)
    # Networking is off in headless runs, so tests can run next to a real copy
    assert env.start(headless=True)
    assert _net._listener is None
    try:
        assert env.start(headless=False)
        assert _net._listener is not None and env.fake.advertised[-1] == ("desk", port)
        assert "1 message handler" in env.fake.loaded[-1][1]
        _net.send(token, "lap", f"127.0.0.1:{port}", 0, "greet", ["Ada"], {"punct": "?"})
        kind, id, extra = env.fake.posts[-1]
        assert kind == FIRE_MESSAGE
        assert _runtime.fire(kind, id, extra)
        assert env.fake.logs[-1] == (0, "hello Ada? from lap")
        assert net.sender() is None

        with pytest.raises(net.NetError, match="Nothing on desk handles"):
            _net.send(token, "lap", f"127.0.0.1:{port}", 0, "nope", [], {})
        with pytest.raises(net.NetError, match="different pairing token"):
            _net.send(_net.new_token(), "lap", f"127.0.0.1:{port}", 0, "greet", ["x"], {})
        env.fake.is_paused = True
        with pytest.raises(net.NetError, match="paused"):
            _net.send(token, "lap", f"127.0.0.1:{port}", 0, "greet", ["x"], {})
        env.fake.is_paused = False

        # A name that isn't in DNS is looked for over Bonjour
        env.fake.bonjour["desk-bonjour"] = ("127.0.0.1", port)
        _net._addresses.clear()
        env.fake.bonjour["nosuchhost-mk"] = ("127.0.0.1", port)
        _net.send(token, "lap", "nosuchhost-mk", port, "greet", ["Bo"], {})
        assert _net._addresses["nosuchhost-mk"] == ("127.0.0.1", port)
        with pytest.raises(net.NetError, match="Unable to reach"):
            _net.send(token, "lap", f"127.0.0.1:{free_port()}", 0, "greet", [], {})
        assert net.machines() == ["desk-bonjour", "nosuchhost-mk"]

        # Turning networking off stops the listener
        _net.save_token(str(env.data), None)
        assert env.reload()
        assert _net._listener is None and env.fake.advertised[-1] == ("", 0)
    finally:
        _net.stop()


def test_send_needs_pairing(env):
    assert env.start(headless=False)
    with pytest.raises(net.NetError, match="isn't paired"):
        net.send("desk", "greet")
    assert net.name() == socket.gethostname()


def test_pairing_from_the_menu(env):
    (env.data / "settings.json").write_text(json.dumps({"net_port": free_port(), "net_listen": "127.0.0.1"}))
    assert env.start(headless=False)
    try:
        env.fake.answers["ask_text"] = None
        assert _runtime.action("net-new-token")
        token = _net.read_token(str(env.data))
        assert token is not None
        assert env.fake.dialogs[-1][3] == token
        assert _net._listener is not None

        # Entering a token checks it, and asks again after a typo
        answers = iter(["mknet1-oops", _net.new_token()])
        env.fake.ask_text = lambda prompt, title, initial: env.fake.dialogs.append(("ask_text", prompt, title, initial)) or next(answers)
        env.fake.answers["msgbox"] = "yes"
        assert _runtime.action("net-enter-token")
        assert _net.read_token(str(env.data)) not in (None, token)
        assert "30 characters" in [d for d in env.fake.dialogs if d[0] == "msgbox"][-1][1]

        assert _runtime.action("net-off")
        assert _net.read_token(str(env.data)) is None
    finally:
        del env.fake.ask_text
        _net.stop()


def test_offline_net_send():
    import subprocess
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    code = "from microkeys import net\nnet.send('desk', 'lock')\n"
    env = dict(os.environ, PYTHONPATH=os.path.join(here, "..", "..", "python"))
    out = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, env=env, timeout=60)
    assert out.returncode == 0 and "send 'lock' to desk (only in MicroKeys)" in out.stderr


def test_run_elevated_toggle(env):
    assert env.start(headless=False)
    assert env.fake.config[6] is False
    assert _runtime.action("toggle-elevated")
    assert json.loads((env.data / "settings.json").read_text())["run_elevated"] is True
    assert env.fake.config[6] is True
    env.fake.elevated = True
    assert _runtime.action("toggle-elevated")
    assert env.fake.config[6] is False
    assert "next time it starts" in env.fake.dialogs[-1][1]


def test_elevated_windows(env):
    env.fake.elevated_windows.add(0x202)
    assert windows.find_one(process="Code.exe").elevated
    assert not windows.find_one(process="notepad.exe").elevated


def test_elevated_log_line(env):
    env.fake.elevated = True
    assert env.start()
    assert "Running as administrator" in env.fake.log_text()
