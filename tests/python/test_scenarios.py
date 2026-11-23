# The eight scenarios from the original tests/ folder, on the new API.

BIG = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" * 200


def test_clip(env):
    # State is kept between macro runs, and the clipboard round trips
    env.write("clip.py", """
        from microkeys import command, clip

        a = 1

        @command("test1")
        def test_func_1():
            global a
            a += 1
            print(f"{clip.get()!r} {a}")

        @command("test2")
        def test_func_2():
            global a
            a += 1
            clip.set(f"{a}")

        @command("test3")
        def test_func_3():
            global a
            a += 1
            print(f"{clip.get()} {a}")
    """)
    assert env.start()
    assert env.run("test1") and env.run("test2") and env.run("test3")
    out = [text for level, text in env.fake.logs if level == 1]
    assert out == ["'' 2", "3 4"]


def test_exception(env):
    env.write("exc.py", """
        from microkeys import command

        @command("test")
        def test_func():
            print("Before exception")
            raise Exception("Test Exception")
    """)
    assert env.start()
    assert env.run("test") is False
    log = env.fake.log_text()
    assert "Before exception" in log
    assert 'exc.py", line 7, in test_func' in log
    assert "Exception: Test Exception" in log
    # Our own frames are left out of the traceback
    assert "_runtime.py" not in log
    assert env.fake.toasts and "Test Exception" in env.fake.toasts[-1][1]


def test_import_error(env):
    env.write("imp.py", """
        from microkeys import command
        import package_that_does_not_exist

        @command("test")
        def test_func():
            print("If we get this far, something is wrong")
    """)
    assert env.start() is False
    log = env.fake.log_text()
    assert "ModuleNotFoundError: No module named 'package_that_does_not_exist'" in log
    assert 'imp.py", line 3' in log
    assert env.commands() == []
    assert env.fake.loaded[-1][0] is False


def test_syntax_error(env):
    env.write("syn.py", """
        from microkeys import command

        @command("test")
        def test_func():
            this = is a syntax error and shouldnt compile
    """)
    assert env.start() is False
    log = env.fake.log_text()
    assert "SyntaxError" in log
    assert "line 6" in log or 'line 6' in env.fake.loaded[-1][1]


def test_limits(env):
    # Big strings go through every call that takes text
    env.write("limits.py", f"""
        from microkeys import command, keys, clip, log

        BIG = {BIG!r}

        @command("test")
        def test_func():
            keys.type(BIG)
            log(BIG)
            clip.set(BIG)
            print(clip.get() == BIG)
            print("Test limits is done")
    """)
    assert env.start()
    assert env.run("test")
    assert env.fake.typed() == BIG
    assert any(text == BIG for level, text in env.fake.logs)
    assert env.fake.log_text().endswith("True\nTest limits is done")


def test_mouse(env):
    env.write("mouse.py", """
        from microkeys import command, mouse

        @command("test")
        def test_func():
            mouse.down()
            mouse.up()
            mouse.down("right")
            mouse.up("right")
            mouse.click()
            mouse.click("right")
            print(mouse.position())
            mouse.move(50, 60)
            print(mouse.position())
            mouse.move_by(-25, -25)
            print(mouse.position())
            mouse.scroll(-3)
    """)
    assert env.start()
    assert env.run("test")
    ev = env.fake.mouse_events
    assert ev[:8] == [("left", "down"), ("left", "up"), ("right", "down"), ("right", "up"),
                      ("left", "down"), ("left", "up"), ("right", "down"), ("right", "up")]
    assert ev[-1] == ("wheel", -3, False)
    out = [text for level, text in env.fake.logs if level == 1]
    assert out == ["(10, 10)", "(50, 60)", "(25, 35)"]


def test_press_keys(env):
    env.write("press.py", """
        from microkeys import command, keys

        @command("test")
        def test_func():
            keys.press("shift+a")
            keys.type("bc")
            keys.press("alt+1", "enter", "up")
            keys.type("+{!\\n")
            keys.type("simple test")
    """)
    assert env.start()
    assert env.run("test")
    events = env.fake.sent
    assert events[:4] == [(0, "shift"), (0, "a"), (1, "a"), (1, "shift")]
    assert events[4:6] == [(2, "b"), (2, "c")]
    assert events[6:10] == [(0, "alt"), (0, "1"), (1, "1"), (1, "alt")]
    assert events[10:14] == [(0, "enter"), (1, "enter"), (0, "up"), (1, "up")]
    assert env.fake.typed() == "bc+{!\nsimple test"


def test_windows(env):
    env.write("win.py", """
        from microkeys import command, windows, run

        @command("test")
        def test_func():
            print(f"Active: {windows.active()}")
            for win in windows.all():
                print(f"Window: {win.title} / {win.process}")
            code = windows.find_one(process="code.exe")
            code.activate()
            print(f"Active: {windows.active().title}")
            print(code.bounds)
            code.move(10, 10, 100, 100)
            print(code.bounds, code.state)
    """)
    assert env.start()
    assert env.run("test")
    out = [text for level, text in env.fake.logs if level == 1]
    assert out[0] == "Active: Window('Untitled - Notepad', process='notepad.exe')"
    assert out[1:4] == ["Window: Untitled - Notepad / notepad.exe",
                        "Window: main.py - Visual Studio Code / Code.exe",
                        "Window: Calculator / ApplicationFrameHost.exe"]
    assert out[4] == "Active: main.py - Visual Studio Code"
    assert out[5] == "(0, 0, 1920, 1040)"
    assert out[6] == "(10, 10, 100, 100) normal"
