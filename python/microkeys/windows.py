"""Finding and arranging windows.

* `active` - the window in front
* `all` - every window on the desktop
* `find` / `find_one` - windows by title, process, or class, with `*` and `?` patterns
* `wait_for` - wait for a window to appear
* `monitors` - the monitors and their work areas
* `app` / `match` - limit a hotkey or hotstring to some windows with `when=`
* `Window` - one window: move it, resize it, bring it forward, close it
"""

import re

from ._native import native
from ._bindings import Match


def match(process=None, title=None, class_name=None):
    """A window description for `when=` on hotkeys and hotstrings.

        @hotkey("f5", when=windows.match(title="*- Excel"))

    Each field is a case-insensitive pattern: `*` matches any run of
    characters, `?` matches one.  All the given fields have to match.
    """
    return Match(process=process, title=title, class_name=class_name)


def app(process):
    """Shorthand for `match(process=...)`, for example `app("notepad.exe")`."""
    return Match(process=process)


def _pattern(text):
    # Glob to regex, the same rules the app uses for when=
    out = []
    for ch in text:
        if ch == "*":
            out.append(".*")
        elif ch == "?":
            out.append(".")
        else:
            out.append(re.escape(ch))
    return re.compile("".join(out), re.IGNORECASE | re.DOTALL)


class Monitor:
    """One monitor.  `x`, `y`, `width`, `height` cover the whole screen;
    the `work_` values leave out the taskbar."""

    def __init__(self, index, info):
        self.index = index
        self.x = info["x"]
        self.y = info["y"]
        self.width = info["width"]
        self.height = info["height"]
        self.work_x = info["work_x"]
        self.work_y = info["work_y"]
        self.work_width = info["work_width"]
        self.work_height = info["work_height"]
        self.primary = info["primary"]
        self.dpi = info["dpi"]

    def __repr__(self):
        return f"Monitor({self.index}: {self.width}x{self.height} at {self.x},{self.y}{', primary' if self.primary else ''})"


class Window:
    """A top-level window.  Properties are read fresh each time."""

    def __init__(self, info):
        self.handle = info["handle"]
        self._info = info

    def __repr__(self):
        return f"Window({self.title!r}, process={self.process!r})"

    def __eq__(self, other):
        return isinstance(other, Window) and other.handle == self.handle

    def __hash__(self):
        return hash(self.handle)

    def _refresh(self):
        info = native.window_info(self.handle)
        if info is not None:
            self._info = info
        return info

    @property
    def exists(self):
        """False once the window has closed."""
        return native.window_info(self.handle) is not None

    @property
    def title(self):
        self._refresh()
        return self._info["title"]

    @property
    def class_name(self):
        return self._info["class_name"]

    @property
    def process(self):
        """The program's file name, like "notepad.exe"."""
        return self._info["process"]

    @property
    def pid(self):
        return self._info["pid"]

    @property
    def state(self):
        """"normal", "minimized", or "maximized"."""
        return native.get_state(self.handle)

    @property
    def bounds(self):
        """(x, y, width, height) of the visible frame, or None if it's gone."""
        return native.get_bounds(self.handle)

    @property
    def monitor(self):
        """The index into `monitors()` of the monitor the window is mostly on."""
        return native.window_monitor(self.handle)

    def activate(self):
        """Bring the window to the front, restoring it if it's minimized."""
        return native.activate(self.handle)

    def minimize(self):
        return native.set_state(self.handle, "minimized")

    def maximize(self):
        return native.set_state(self.handle, "maximized")

    def restore(self):
        return native.set_state(self.handle, "normal")

    def close(self):
        """Ask the window to close, as if its close button was clicked."""
        return native.close_window(self.handle)

    def move(self, x=None, y=None, width=None, height=None):
        """Move and resize the window.  Leave out what shouldn't change."""
        cur = native.get_bounds(self.handle)
        if cur is None:
            return False
        if self.state != "normal":
            self.restore()
        new = [cur[0] if x is None else x, cur[1] if y is None else y,
               cur[2] if width is None else width, cur[3] if height is None else height]
        return native.set_bounds(self.handle, *[int(v) for v in new])

    def move_to_monitor(self, index, fill=False):
        """Move the window to another monitor, keeping its size, centered,
        or filling the monitor's work area with `fill`."""
        mons = monitors()
        mon = mons[index % len(mons)]
        if fill:
            return self.move(mon.work_x, mon.work_y, mon.work_width, mon.work_height)
        cur = self.bounds
        if cur is None:
            return False
        width = min(cur[2], mon.work_width)
        height = min(cur[3], mon.work_height)
        return self.move(mon.work_x + (mon.work_width - width) // 2, mon.work_y + (mon.work_height - height) // 2, width, height)


def active():
    """The window in front, or None."""
    info = native.foreground()
    return None if info is None else Window(info)


def all(include_hidden=False):
    """Every window on the desktop, front to back.

    By default only visible windows with a title, the ones a person would
    see in Alt+Tab.
    """
    return [Window(info) for info in native.list_windows(bool(include_hidden))]


def find(title=None, process=None, class_name=None, include_hidden=False):
    """Every window matching all the given patterns, front to back.

        windows.find(process="code.exe")
        windows.find(title="*- Notepad")

    Patterns are case-insensitive, with `*` and `?` as wildcards.  `title`
    can also be a function that's given the title and returns True or False.
    """
    tests = []
    for field, want in (("title", title), ("process", process), ("class_name", class_name)):
        if want is None:
            continue
        if callable(want):
            tests.append((field, want))
        else:
            tests.append((field, _pattern(want).fullmatch))
    ret = []
    for info in native.list_windows(bool(include_hidden)):
        if _all_true(test(info[field]) for field, test in tests):
            ret.append(Window(info))
    return ret


def _all_true(values):
    for value in values:
        if not value:
            return False
    return True


def find_one(title=None, process=None, class_name=None):
    """The front-most window matching the patterns, or None."""
    found = find(title=title, process=process, class_name=class_name)
    return found[0] if found else None


def wait_for(title=None, process=None, class_name=None, timeout=10.0, active=False):
    """Wait up to `timeout` seconds for a matching window to appear (and,
    with `active=True`, to be in front).  Returns it, or None."""
    waited = 0.0
    while True:
        front = native.foreground()
        for win in find(title=title, process=process, class_name=class_name):
            if not active or (front is not None and front["handle"] == win.handle):
                return win
        if waited >= timeout:
            return None
        native.sleep(0.1)
        waited += 0.1


def monitors():
    """Every monitor, the main one first."""
    return [Monitor(i, info) for i, info in enumerate(native.monitors())]
