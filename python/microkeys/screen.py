"""The screen: colours, screenshots, and finding pictures and text on it.

* `pixel` - the colour at a point
* `capture` - a screenshot of the screen, a window, a monitor, or an area
* `load` - an image from a file, to look for
* `find` / `find_all` - where an image appears on screen
* `wait_for` - wait for an image to appear
* `wait_for_color` - wait for a point to turn a colour
* `find_text` / `find_all_text` - where some text appears on screen
* `wait_for_text` - wait for some text to appear
* `read_text` - all the text on screen, read with OCR
* `text_languages` - the languages OCR can read
* `Image` - pixels from the screen or a file: crop them, save them, search them

Coordinates are screen pixels, as in the mouse module.  Colours are
(red, green, blue) tuples, each 0 to 255; anywhere a colour is taken, a
string like "#ff8000" works too.

To find a button and click it:

    found = screen.wait_for("ok_button.png", timeout=5)
    if found:
        found.click()

Make the picture to look for with `capture(...).save("ok_button.png")`, or
crop a screenshot in any image editor.  Look for something small and
distinctive; pixels that are fully transparent in a PNG match anything.

To click some text, wherever it is:

    screen.find_text("Click here!").click()

Text is found through accessibility (the text web browsers and most programs
tell screen readers), and else read off the screen with OCR, which works on
anything that shows text, pictures of it included.  Both run on your own
computer.
"""

import os
import time
import unicodedata
from collections import namedtuple

from ._native import native

__all__ = ["pixel", "capture", "load", "find", "find_all", "wait_for", "wait_for_color", "find_text", "find_all_text",
           "wait_for_text", "read_text", "text_languages", "Image", "Found", "Word", "TextLine"]

METHODS = ("auto", "controls", "ocr")


class Found(namedtuple("Found", ["x", "y", "width", "height"])):
    """Where an image or text was found: its top left corner and size on
    screen."""

    @property
    def center(self):
        """The (x, y) of the middle, for clicking."""
        return (self.x + self.width // 2, self.y + self.height // 2)

    def click(self, button="left", count=1):
        """Click in the middle of it."""
        from . import mouse
        x, y = self.center
        mouse.click(button, count, x, y)


class Word(namedtuple("Word", ["text", "x", "y", "width", "height"])):
    """A word read off the screen, and where it is."""

    @property
    def center(self):
        return (self.x + self.width // 2, self.y + self.height // 2)


class TextLine:
    """A line of text read off the screen: `text`, its `words`, and where it
    is (`x`, `y`, `width`, `height`)."""

    def __init__(self, words):
        self.words = list(words)
        self.text = " ".join(w.text for w in self.words)
        self.x = min(w.x for w in self.words)
        self.y = min(w.y for w in self.words)
        self.width = max(w.x + w.width for w in self.words) - self.x
        self.height = max(w.y + w.height for w in self.words) - self.y

    def __repr__(self):
        return f"TextLine({self.text!r} at {self.x},{self.y})"

    @property
    def center(self):
        return (self.x + self.width // 2, self.y + self.height // 2)


def _color(value):
    # (r, g, b) from a tuple or "#rrggbb"
    if isinstance(value, str):
        text = value.strip().lstrip("#")
        if len(text) == 3:
            text = "".join(ch * 2 for ch in text)
        if len(text) != 6:
            raise ValueError(f"Expected a colour like '#ff8000', got {value!r}")
        try:
            return (int(text[0:2], 16), int(text[2:4], 16), int(text[4:6], 16))
        except ValueError:
            raise ValueError(f"Expected a colour like '#ff8000', got {value!r}") from None
    r, g, b = value[:3]
    return (int(r), int(g), int(b))


class Image:
    """Pixels from the screen or a file.

    `x` and `y` are where its top left corner was on screen (0, 0 for an
    image from a file), so positions found inside a screenshot can be turned
    back into screen positions.
    """

    def __init__(self, width, height, pixels, x=0, y=0):
        if len(pixels) != width * height * 4:
            raise ValueError("The pixels don't fit the size")
        self.width = width
        self.height = height
        self.x = x
        self.y = y
        # How much OCR scales it up first: text on a monitor with a high DPI
        # is already big enough
        self._ocr_scale = 2
        # Four bytes per pixel, top row first: blue, green, red, alpha
        self._pixels = bytes(pixels)

    def __repr__(self):
        return f"Image({self.width}x{self.height} at {self.x},{self.y})"

    @property
    def size(self):
        return (self.width, self.height)

    def pixel(self, x, y):
        """The (red, green, blue) at (x, y) within the image."""
        if not (0 <= x < self.width and 0 <= y < self.height):
            raise IndexError(f"({x}, {y}) is outside the {self.width}x{self.height} image")
        i = (y * self.width + x) * 4
        b, g, r = self._pixels[i:i + 3]
        return (r, g, b)

    def crop(self, x, y, width, height):
        """The part of the image with its top left at (x, y)."""
        x, y = max(0, int(x)), max(0, int(y))
        width = min(int(width), self.width - x)
        height = min(int(height), self.height - y)
        if width <= 0 or height <= 0:
            raise ValueError("Nothing of the image is left after cropping")
        stride = self.width * 4
        rows = [self._pixels[(y + row) * stride + x * 4:(y + row) * stride + (x + width) * 4] for row in range(height)]
        part = Image(width, height, b"".join(rows), self.x + x, self.y + y)
        part._ocr_scale = self._ocr_scale
        return part

    def png(self):
        """The image as PNG data, for clip.set(image=...) or saving."""
        return native.image_encode(self.width, self.height, self._pixels)

    def save(self, path):
        """Save the image as a PNG file."""
        data = self.png()
        with open(path, "wb") as f:
            f.write(data)

    def find(self, image, tolerance=0):
        """Where `image` appears inside this one, as a `Found` in screen
        coordinates, or None."""
        found = self.find_all(image, tolerance, limit=1)
        return found[0] if found else None

    def find_all(self, image, tolerance=0, limit=100):
        """Every place `image` appears inside this one, top to bottom."""
        needle = load(image)
        hits = native.image_find(self.width, self.height, self._pixels, needle.width, needle.height, needle._pixels,
                                 int(tolerance), int(limit))
        return [Found(self.x + x, self.y + y, needle.width, needle.height) for x, y in hits]

    def read_text(self, language=None):
        """The text in the image, read with OCR, as `TextLine`s in reading
        order.  `language` is a tag like "fr-FR"; without one, it's yours."""
        words = native.text_read(self.width, self.height, self._pixels, language or "", self._ocr_scale)
        lines = {}
        for text, x, y, width, height, line in words:
            lines.setdefault(line, []).append(Word(text, self.x + x, self.y + y, width, height))
        return [TextLine(lines[n]) for n in sorted(lines)]

    def find_text(self, text, match_case=False, language=None):
        """Where `text` is in the image, read with OCR, as a `Found` in
        screen coordinates, or None."""
        found = self.find_all_text(text, match_case, language, limit=1)
        return found[0] if found else None

    def find_all_text(self, text, match_case=False, language=None, limit=100):
        """Every place `text` is in the image, in reading order."""
        found = []
        for line in self.read_text(language):
            for first, last in _match_words([w.text for w in line.words], _phrase(text), match_case):
                found.append(_around(line.words[first:last + 1]))
                if len(found) >= limit:
                    return found
        return found


def _bounds(region):
    # (x, y, width, height) for a region given as a tuple, a Window, or a
    # Monitor; None means every monitor
    from . import windows
    if region is None:
        mons = windows.monitors()
        left = min(m.x for m in mons)
        top = min(m.y for m in mons)
        right = max(m.x + m.width for m in mons)
        bottom = max(m.y + m.height for m in mons)
        return (left, top, right - left, bottom - top)
    if isinstance(region, windows.Window):
        bounds = region.bounds
        if bounds is None:
            raise ValueError(f"{region!r} has closed")
        return bounds
    if isinstance(region, windows.Monitor):
        return (region.x, region.y, region.width, region.height)
    x, y, width, height = region
    return (int(x), int(y), int(width), int(height))


def capture(region=None):
    """A screenshot, as an `Image`.

        screen.capture().save("everything.png")
        screen.capture(windows.active()).save("window.png")
        screen.capture((100, 100, 400, 300))

    `region` is an (x, y, width, height) tuple, a `windows.Window`, or a
    `windows.Monitor`; without one, every monitor is captured.
    """
    x, y, width, height = _bounds(region)
    if width <= 0 or height <= 0:
        raise ValueError("The area to capture is empty")
    shot = Image(width, height, native.screen_capture(x, y, width, height), x, y)
    shot._ocr_scale = _ocr_scale((x, y, width, height))
    return shot


def _ocr_scale(area):
    # 2 at 100% and 125% scaling, 1 from 150%, going by the monitors the
    # area is on
    from . import windows
    x, y, width, height = area
    dpis = [m.dpi for m in windows.monitors()
            if m.x < x + width and x < m.x + m.width and m.y < y + height and y < m.y + m.height]
    return 2 if not dpis or min(dpis) < 144 else 1


def pixel(x, y):
    """The (red, green, blue) colour of the screen at (x, y)."""
    return capture((x, y, 1, 1)).pixel(0, 0)


def load(source):
    """An `Image` from a file (PNG, BMP, JPEG, GIF, TIFF), or from the bytes
    of one.  An `Image` is returned as it is."""
    if isinstance(source, Image):
        return source
    if isinstance(source, (bytes, bytearray)):
        data = bytes(source)
    else:
        with open(os.fspath(source), "rb") as f:
            data = f.read()
    width, height, pixels = native.image_decode(data)
    return Image(width, height, pixels)


def find(image, region=None, tolerance=0):
    """Where `image` is on screen, as a `Found`, or None.

        found = screen.find("save_icon.png")
        if found:
            found.click()

    `image` is a file name or an `Image`.  `region` limits the search, as for
    `capture()`, and makes it faster.  `tolerance` lets each of red, green,
    and blue differ by that much (0 to 255), for colours that shift a
    little; 10 to 30 is usual.  The first match from the top left wins.
    """
    needle = load(image)
    return capture(region).find(needle, tolerance)


def find_all(image, region=None, tolerance=0, limit=100):
    """Every place `image` is on screen, top to bottom, as `Found`s."""
    needle = load(image)
    return capture(region).find_all(needle, tolerance, limit)


def wait_for(image, timeout=10.0, region=None, tolerance=0, interval=0.25):
    """Wait up to `timeout` seconds for `image` to appear on screen.
    Returns where, as a `Found`, or None."""
    needle = load(image)
    waited = 0.0
    while True:
        found = capture(region).find(needle, tolerance)
        if found is not None or waited >= timeout:
            return found
        native.sleep(interval)
        waited += interval


def wait_for_color(x, y, color, timeout=10.0, tolerance=0, interval=0.1):
    """Wait up to `timeout` seconds for the screen at (x, y) to be `color`.
    Returns True if it was, False if time ran out."""
    want = _color(color)
    waited = 0.0
    while True:
        have = pixel(x, y)
        if all(abs(a - b) <= tolerance for a, b in zip(have, want)):
            return True
        if waited >= timeout:
            return False
        native.sleep(interval)
        waited += interval


# Text.  Matching compares letters and digits only, since OCR often misreads
# spaces and punctuation, and only whole words match: "here" isn't found in
# "where".  Scripts written without spaces between words (Chinese, Japanese)
# have no word edges to keep to.

def _phrase(text):
    if not isinstance(text, str):
        raise TypeError("The text to find must be a string")
    text = " ".join(text.split())
    if not any(ch.isalnum() for ch in text):
        raise ValueError("The text to find needs a letter or digit in it")
    return text


def _unspaced(ch):
    return unicodedata.east_asian_width(ch) in ("W", "F")


def _wordy(ch):
    return ch.isalnum() and not _unspaced(ch)


def _letters(text, match_case):
    return [ch for ch in (text if match_case else text.casefold()) if ch.isalnum()]


def _match_words(words, text, match_case):
    # Runs of words, as (first, last) indexes, that spell out text
    want = "".join(_letters(text, match_case))
    chars, owner, starts, ends = [], [], [], []
    for i, word in enumerate(words):
        letters = _letters(word, match_case)
        for j, ch in enumerate(letters):
            chars.append(ch)
            owner.append(i)
            starts.append(j == 0)
            ends.append(j == len(letters) - 1)
    have = "".join(chars)
    found = []
    at = have.find(want) if want else -1
    while at != -1:
        end = at + len(want) - 1
        if ((starts[at] or _unspaced(have[at]) or _unspaced(have[at - 1])) and
                (ends[end] or _unspaced(have[end]) or _unspaced(have[end + 1]))):
            found.append((owner[at], owner[end]))
        at = have.find(want, at + 1)
    return found


def _around(rects):
    # The Found that covers every rectangle (or Word)
    boxes =[(r.x, r.y, r.width, r.height) if isinstance(r, Word) else tuple(r) for r in rects]
    left = min(b[0] for b in boxes)
    top = min(b[1] for b in boxes)
    right = max(b[0] + b[2] for b in boxes)
    bottom = max(b[1] + b[3] for b in boxes)
    return Found(left, top, right - left, bottom - top)


def _first_line(rects):
    # Text that wraps has a rectangle per line; the first line is where it
    # starts, and any rectangles beside it are the same line
    x, y, width, height = rects[0]
    return _around([r for r in rects if r[1] < y + height and r[1] + r[3] > y])


def _inside(point, box):
    x, y = point
    return box[0] <= x < box[0] + box[2] and box[1] <= y < box[1] + box[3]


def _edges(text, before, after):
    # A match in a document only counts if it's whole words
    return not ((before and _wordy(before[-1]) and _wordy(text[0])) or (after and _wordy(after[0]) and _wordy(text[-1])))


def _whole_match(text, found, context, match_case):
    # Whether a document's match is really the text, as whole words.  What
    # it found has to spell the text (one browser sometimes hands back the
    # wrong stretch), and the characters around it in the context mustn't
    # carry the word on.
    if _letters(found, match_case) != _letters(text, match_case):
        return False
    at = context.lower().find(found.lower())
    if at < 0:
        return True
    end = at + len(found)
    return _edges(text, context[at - 1] if at > 0 else "", context[end] if end < len(context) else "")


def _accessible_text(text, window, match_case, limit):
    # Text found through UI Automation, as (Found, exact) pairs.  Matches in
    # documents are exact; a control whose name has more than the text in it
    # gives only where the whole control is.
    # Each search may turn up parts of words, which don't count, so it's
    # allowed more than are wanted
    raw = max(limit * 5, 20)
    found = []
    for in_document, rects, matched, context, name in native.control_text_find(0, window, text, match_case, True, raw):
        spot = _first_line(rects)
        if _whole_match(text, matched, context, match_case) and spot not in [f for f, exact in found]:
            found.append((spot, True))
    if len(found) >= limit:
        return found[:limit]
    want = _letters(text, match_case)
    for in_document, rects, matched, context, name in native.control_text_find(0, window, text, match_case, False, raw):
        spot = Found(*rects[0])
        if not _match_words(name.split(), text, match_case):
            continue
        # The same text as a match in a document, say a link in a web page
        if any(_inside(f.center, spot) for f, exact in found):
            continue
        found.append((spot, _letters(name, match_case) == want))
        if len(found) >= limit:
            break
    return found


def _monitor_areas(region):
    # OCR reads one monitor at a time, since there's a limit to how big an
    # image it takes, and a line of text is never split between monitors
    from . import windows
    if region is None:
        return [(m.x, m.y, m.width, m.height) for m in windows.monitors()]
    return [_bounds(region)]


def _ocr_text(text, region, match_case, language, limit):
    found = []
    for area in _monitor_areas(region):
        if area[2] > 0 and area[3] > 0:
            found += capture(area).find_all_text(text, match_case, language, limit - len(found))
        if len(found) >= limit:
            break
    return found


def _pinpoint(spot, text, match_case, language):
    # Where the text is inside a control that has more text than that, read
    # with OCR; the whole control if it can't be read
    pad = 4
    try:
        within = capture((spot.x - pad, spot.y - pad, spot.width + pad * 2, spot.height + pad * 2))
        return within.find_text(text, match_case, language) or spot
    except OSError:
        return spot


def find_text(text, region=None, method="auto", match_case=False, language=None):
    """Where `text` is on screen, as a `Found`, or None.

        screen.find_text("Click here!").click()
        found = screen.find_text("Sign in", region=windows.find_one(process="firefox.exe"))

    Letters and digits are compared, not spaces or punctuation, and case
    doesn't matter unless `match_case` is true.  Only whole words match.
    Text that's scrolled away or covered by another window isn't found.

    `region` is a `windows.Window`, a `windows.Monitor`, or an (x, y, width,
    height) tuple; without one, the whole screen.  `method` is how to look:

    * "controls" - through accessibility, in the window `region` is, or the
      window in front.  Exact and fast, and it finds text in web pages, but
      only in programs that tell screen readers what they show.
    * "ocr" - by reading the screen with OCR, which finds any text that can
      be seen, pictures of text included, but can misread small, faint, or
      fancy text.  It takes a few tenths of a second per monitor.
    * "auto" (the default) - accessibility first, then OCR.

    `language` is the language for OCR, a tag like "fr-FR" (see
    `text_languages()`); without one, it's yours.
    """
    found = find_all_text(text, region, method, match_case, language, limit=1)
    return found[0] if found else None


def find_all_text(text, region=None, method="auto", match_case=False, language=None, limit=100):
    """Every place `text` is on screen, as `Found`s, in the order they were
    found.  The arguments are as for `find_text()`."""
    from . import windows
    text = _phrase(text)
    if method not in METHODS:
        raise ValueError(f"method is one of {', '.join(METHODS)}, not {method!r}")
    if method != "ocr":
        window = region if isinstance(region, windows.Window) else None
        area = None if region is None or window is not None else _bounds(region)
        try:
            hits = _accessible_text(text, window.handle if window else 0, match_case, limit)
        except OSError:
            if method == "controls":
                raise
            hits = []
        found = []
        for spot, exact in hits:
            if area is not None and not _inside(spot.center, area):
                continue
            if not exact and method == "auto":
                spot = _pinpoint(spot, text, match_case, language)
            found.append(spot)
        if found or method == "controls":
            return found
    return _ocr_text(text, region, match_case, language, limit)


def wait_for_text(text, timeout=10.0, region=None, method="auto", match_case=False, language=None, interval=0.5):
    """Wait up to `timeout` seconds for `text` to appear on screen.  Returns
    where, as a `Found`, or None.  The arguments are as for `find_text()`."""
    waited = 0.0
    while True:
        started = time.monotonic()
        found = find_text(text, region, method, match_case, language)
        if found is not None or waited >= timeout:
            return found
        native.sleep(interval)
        waited += interval + (time.monotonic() - started)


def read_text(region=None, language=None):
    """All the text on screen, read with OCR, as `TextLine`s in reading
    order.  Each line has its `text` and its `words`, with where each one is.

        for line in screen.read_text(windows.active()):
            print(line.text)

    `region` and `language` are as for `find_text()`.
    """
    lines = []
    for area in _monitor_areas(region):
        if area[2] > 0 and area[3] > 0:
            lines += capture(area).read_text(language)
    return lines


def text_languages():
    """The languages OCR can read, as tags like "en-US".  More can be added
    in Windows' Settings, under Time & language, Language & region."""
    return native.text_languages()
