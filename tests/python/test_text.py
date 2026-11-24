# Finding text on screen: read with OCR, or found through accessibility, and
# the rules for what counts as a match.

import pytest

from microkeys import screen, windows


def small_monitors(monkeypatch, fake):
    # Two little monitors, so captures of "everything" stay quick
    mons = [{"x": 0, "y": 0, "width": 64, "height": 48, "work_x": 0, "work_y": 0, "work_width": 64, "work_height": 44, "primary": True, "dpi": 96},
            {"x": 64, "y": 0, "width": 32, "height": 40, "work_x": 64, "work_y": 0, "work_width": 32, "work_height": 40, "primary": False, "dpi": 96}]
    monkeypatch.setattr(fake, "monitors", lambda: mons)


def paint_words(fake):
    fake.screen_words = [
        ("Somewhere", 2, 2, 20, 6, 0), ("over", 24, 2, 10, 6, 0), ("here!", 36, 2, 12, 6, 0),
        ("Click", 2, 20, 12, 6, 1), ("here!", 16, 20, 12, 6, 1),
        ("Clickhere", 66, 10, 24, 6, 0),
    ]


def test_matching_rules():
    match = screen._match_words
    assert match(["Click", "here!"], "Click here!", False) == [(0, 1)]
    assert match(["click", "HERE"], "Click here!", False) == [(0, 1)]
    assert match(["click", "HERE"], "Click here!", True) == []
    # Only whole words
    assert match(["Somewhere", "else"], "here", False) == []
    assert match(["where", "here", "there"], "here", False) == [(1, 1)]
    assert match(["Downloads"], "Download", False) == []
    # OCR's spaces and punctuation don't count
    assert match(["Clickhere"], "Click here", False) == [(0, 0)]
    assert match(["(Click", "here)"], "click here", False) == [(0, 1)]
    assert match(["e-mail"], "email", False) == [(0, 0)]
    # Every match, and none from an empty line
    assert match(["ok", "OK", "Ok."], "ok", False) == [(0, 0), (1, 1), (2, 2)]
    assert match([], "ok", False) == []
    # Chinese has no spaces, so any run of characters counts
    assert match(["东京都", "大学"], "京都", False) == [(0, 0)]
    with pytest.raises(ValueError):
        screen._phrase("  !! ")
    with pytest.raises(TypeError):
        screen._phrase(None)
    assert screen._phrase("  Click \n here ") == "Click here"
    # A match in a document, with the text around it
    assert screen._whole_match("lazy dog", "lazy dog", "the lazy dog and", False)
    assert screen._whole_match("lazy dog", "Lazy  Dog", "the Lazy  Dog and", False)
    assert not screen._whole_match("lazy dog", "Lazy Dog", "Lazy Dog", True)
    assert not screen._whole_match("where", "where", "Nowhere t", False)
    assert not screen._whole_match("where", "e fou", "be found", False)
    assert screen._whole_match("where", "where", "garbled", False)
    assert screen._edges("here", " ", "!")
    assert not screen._edges("here", "w", " ")
    assert not screen._edges("here", "", "s")
    assert screen._edges("!here", "x", "")
    assert screen._edges("京都", "东", "大")


def test_read_text(env, monkeypatch):
    fake = env.fake
    small_monitors(monkeypatch, fake)
    paint_words(fake)
    lines = screen.read_text()
    # One monitor at a time
    assert [r[0] for r in fake.ocr_reads] == [(0, 0, 64, 48), (64, 0, 32, 40)]
    assert [line.text for line in lines] == ["Somewhere over here!", "Click here!", "Clickhere"]
    assert (lines[1].x, lines[1].y, lines[1].width, lines[1].height) == (2, 20, 26, 6)
    assert lines[1].words[1] == ("here!", 16, 20, 12, 6) and lines[1].words[1].center == (22, 23)
    # Positions are on screen, not in the picture
    shot = screen.capture((10, 15, 30, 20))
    assert [(w.text, w.x, w.y) for line in shot.read_text("fr-FR") for w in line.words] == [("here!", 16, 20)]
    assert fake.ocr_reads[-1] == ((10, 15, 30, 20), "fr-FR", 2)
    # Text on a high DPI monitor is big enough as it is
    fake.monitors()[1]["dpi"] = 192
    screen.capture((70, 5, 10, 10)).crop(1, 1, 8, 8).read_text()
    assert fake.ocr_reads[-1][2] == 1
    screen.capture((60, 5, 10, 10)).read_text()
    assert fake.ocr_reads[-1][2] == 2
    assert screen.text_languages() == ["en-US"]
    fake.ocr_error = "OCR for de-DE isn't installed"
    with pytest.raises(OSError, match="de-DE"):
        screen.read_text(language="de-DE")


def test_find_text_with_ocr(env, monkeypatch):
    fake = env.fake
    small_monitors(monkeypatch, fake)
    paint_words(fake)
    found = screen.find_text("click here!", method="ocr")
    assert found == (2, 20, 26, 6) and found.center == (15, 23)
    assert screen.find_all_text("here", method="ocr") == [(36, 2, 12, 6), (16, 20, 12, 6)]
    assert screen.find_all_text("here", method="ocr", limit=1) == [(36, 2, 12, 6)]
    # The second monitor is read only when the first didn't have enough
    assert screen.find_all_text("click here", method="ocr") == [(2, 20, 26, 6), (66, 10, 24, 6)]
    assert screen.find_text("Click Here", method="ocr", match_case=True) is None
    assert screen.find_text("where", method="ocr") is None
    assert screen.find_text("Click", region=(0, 0, 40, 15), method="ocr") is None
    assert fake.captures[-1] == (0, 0, 40, 15)

    # Clicking it
    found.click()
    assert fake.mouse_events[-3:] == [("move", 15, 23), ("left", "down"), ("left", "up")]
    with pytest.raises(ValueError):
        screen.find_text("x", method="smell")


def test_find_text_with_controls(env, monkeypatch):
    fake = env.fake
    small_monitors(monkeypatch, fake)
    paint_words(fake)
    fake.text_hits = {
        # A part of a word, a stretch that isn't the text at all, a wrapped
        # match, and the same place twice
        True: [(True, [(5, 5, 10, 4)], "here", "somewhere ", ""), (True, [(8, 8, 10, 4)], "e fo", "be found", ""),
               (True, [(30, 30, 20, 4), (50, 30, 6, 4), (0, 34, 9, 4)], "HERE", "s, here.", ""),
               (True, [(30, 30, 26, 4)], "here", "here", "")],
        # A link already found in the document, a button, a part of a word,
        # and a label with more text
        False: [(False, [(28, 28, 30, 10)], "", "", "here"), (False, [(40, 40, 10, 6)], "", "", "Here"),
                (False, [(0, 0, 9, 9)], "", "", "nowhere"), (False, [(2, 18, 50, 10)], "", "", "Look over here, please")],
    }
    found = screen.find_all_text("here", method="controls")
    assert found == [(30, 30, 26, 4), (40, 40, 10, 6), (2, 18, 50, 10)]
    assert fake.text_searches[-2:] == [(0, "here", False, True, 500), (0, "here", False, False, 500)]
    # Enough in documents means controls' names aren't searched
    count = len(fake.text_searches)
    assert screen.find_text("here", method="controls") == (30, 30, 26, 4)
    assert [s[3] for s in fake.text_searches[count:]] == [True]

    # A window to search in, and an area to keep to
    notepad = windows.find_one(process="notepad.exe")
    screen.find_text("here", region=notepad, method="controls", match_case=True)
    assert fake.text_searches[-1][:3] == (notepad.handle, "here", True)
    assert screen.find_all_text("here", region=(35, 35, 20, 20), method="controls") == [(40, 40, 10, 6)]

    # "auto" finds the label's text inside it with OCR, and takes the rest as
    # they are
    found = screen.find_all_text("here", method="auto")
    assert found == [(30, 30, 26, 4), (40, 40, 10, 6), (16, 20, 12, 6)]

    # With nothing through accessibility, "auto" reads the screen
    fake.text_hits = {True: [], False: []}
    assert screen.find_text("click here") == (2, 20, 26, 6)
    fake.text_hits = OSError("The window didn't answer")
    assert screen.find_text("click here") == (2, 20, 26, 6)
    with pytest.raises(OSError):
        screen.find_text("click here", method="controls")


def test_wait_for_text(env, monkeypatch):
    fake = env.fake
    small_monitors(monkeypatch, fake)
    assert screen.wait_for_text("Ready", timeout=1, method="ocr") is None
    assert fake.slept >= 1
    fake.screen_words = [("Ready", 4, 4, 20, 8, 0)]
    assert screen.wait_for_text("ready", timeout=1) == (4, 4, 20, 8)
