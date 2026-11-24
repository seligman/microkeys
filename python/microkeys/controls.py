"""The controls inside windows: buttons, text boxes, check boxes, menus.

* `find` / `find_all` - controls by name, role, automation id, or class
* `wait_for` - wait for a control to appear
* `focused` - the control with the keyboard focus
* `at` - the control at a point on screen
* `Control` - one control: read it, click it, type into it
* `ROLES` - the roles controls can have

This uses UI Automation, which most programs support, so it keeps working
when a window moves or its layout changes, where clicking at coordinates
doesn't.

    @hotkey("ctrl+alt+s")
    def save_without_asking():
        controls.find(name="Save", role="button").click()

    box = controls.find(role="edit", automation_id="SearchBox", window=windows.find_one(process="myapp.exe"))
    box.value = "invoices"

Names and class names are case-insensitive patterns with `*` and `?`, as
for windows.  To see what a program's controls are called, hover over one
and run `controls.at(*mouse.position())` at the log window's prompt, or use
Accessibility Insights for Windows.
"""

from ._native import native

__all__ = ["find", "find_all", "wait_for", "focused", "at", "Control", "ROLES"]

ROLES = ["app_bar", "button", "calendar", "checkbox", "combobox", "custom", "datagrid", "dataitem", "document",
         "edit", "group", "header", "headeritem", "hyperlink", "image", "list", "listitem", "menu", "menubar",
         "menuitem", "pane", "progressbar", "radiobutton", "scrollbar", "semanticzoom", "separator", "slider",
         "spinner", "splitbutton", "statusbar", "tab", "tabitem", "table", "text", "thumb", "titlebar", "toolbar",
         "tooltip", "tree", "treeitem", "window"]


class Control:
    """One control in a window.  Properties are read fresh each time."""

    def __init__(self, handle):
        self._handle = handle

    def __del__(self):
        try:
            native.control_release(self._handle)
        except Exception:
            pass

    def __repr__(self):
        info = self._info()
        if info is None:
            return "Control(gone)"
        return f"Control({info['role']}, name={info['name']!r})"

    def __eq__(self, other):
        if not isinstance(other, Control):
            return False
        mine = self._get("runtime_id")
        return mine is not None and mine == other._get("runtime_id")

    def __hash__(self):
        return hash(self._get("runtime_id"))

    def _info(self):
        return native.control_info(self._handle)

    def _get(self, key, default=None):
        info = self._info()
        return default if info is None else info[key]

    @property
    def exists(self):
        """False once the control is gone, say its window closed."""
        return self._info() is not None

    @property
    def name(self):
        """What the control is called: a button's caption, a text box's label."""
        return self._get("name", "")

    @property
    def role(self):
        """What kind of control it is, one of `ROLES`."""
        return self._get("role", "")

    @property
    def automation_id(self):
        """The id its program gave it, which doesn't change with the language."""
        return self._get("automation_id", "")

    @property
    def class_name(self):
        return self._get("class_name", "")

    @property
    def enabled(self):
        return self._get("enabled", False)

    @property
    def focused(self):
        """True if it has the keyboard focus."""
        return self._get("focused", False)

    @property
    def visible(self):
        """False if it's scrolled or folded out of sight."""
        return not self._get("offscreen", True)

    @property
    def bounds(self):
        """(x, y, width, height) on screen."""
        return self._get("bounds")

    @property
    def center(self):
        """The (x, y) of its middle, for the mouse."""
        b = self.bounds
        return None if b is None else (b[0] + b[2] // 2, b[1] + b[3] // 2)

    @property
    def window(self):
        """The `windows.Window` it's in."""
        from . import windows
        handle = self._get("window", 0)
        info = native.window_info(handle) if handle else None
        return None if info is None else windows.Window(info)

    @property
    def value(self):
        """The text in a text box, combo box, or similar, or None for
        controls that don't hold a value.  Set it to change the text."""
        info = self._info()
        return None if info is None or not info["has_value"] else info["value"]

    @value.setter
    def value(self, text):
        self.set_value(text)

    @property
    def text(self):
        """All the text the control shows: a document's contents, a text
        box's value, or else its name."""
        ret = native.control_text(self._handle)
        return "" if ret is None else ret

    @property
    def checked(self):
        """True or False for a check box or toggle button, None for others
        (and for a check box that's neither, in a tree of them)."""
        state = self._get("toggle", -1)
        return None if state not in (0, 1) else state == 1

    @property
    def selected(self):
        """True or False for a list item, tab, or radio button, None for others."""
        state = self._get("selected", -1)
        return None if state < 0 else state == 1

    @property
    def expanded(self):
        """True or False for a tree item, combo box, or menu that opens, None for others."""
        state = self._get("expanded", -1)
        return None if state < 0 else state == 1

    @property
    def actions(self):
        """What the control can do: some of "click", "toggle", "select",
        "expand", "collapse", "set_value", and "scroll"."""
        return list(self._get("actions", []))

    @property
    def parent(self):
        found = native.control_related(self._handle, True)
        return Control(found[0]) if found else None

    @property
    def children(self):
        return [Control(h) for h in native.control_related(self._handle, False)]

    def find(self, name=None, role=None, automation_id=None, class_name=None):
        """The first control inside this one that matches, or None."""
        found = _find(self._handle, 0, name, role, automation_id, class_name, False)
        return found[0] if found else None

    def find_all(self, name=None, role=None, automation_id=None, class_name=None):
        """Every control inside this one that matches."""
        return _find(self._handle, 0, name, role, automation_id, class_name, True)

    def _do(self, action, value=""):
        native.control_do(self._handle, action, value)

    def click(self):
        """Press it: click a button or menu item, flip a check box, pick a
        list item, or open a menu.  Controls that can't be pressed that way
        are clicked with the mouse, in their middle."""
        actions = self.actions
        for action in ("click", "toggle", "select", "expand"):
            if action in actions:
                self._do(action)
                return
        center = self.center
        if center is None:
            raise ValueError(f"{self!r} can't be clicked")
        from . import mouse
        mouse.click(x=center[0], y=center[1])

    def set_value(self, text):
        """Replace the text in a text box or similar, without typing it."""
        self._do("set_value", str(text))

    def focus(self):
        """Give it the keyboard focus, so keys a macro types go to it."""
        self._do("focus")

    def toggle(self):
        """Flip a check box or toggle button."""
        self._do("toggle")

    def select(self):
        """Select a list item, tab, or radio button."""
        self._do("select")

    def expand(self):
        """Open a tree item, combo box, or menu."""
        self._do("expand")

    def collapse(self):
        """Close a tree item, combo box, or menu."""
        self._do("collapse")

    def scroll_into_view(self):
        """Scroll its list or page until it can be seen."""
        self._do("scroll")


def _window_handle(window):
    if window is None:
        return 0
    handle = getattr(window, "handle", window)
    if not isinstance(handle, int):
        raise TypeError("window= takes a windows.Window")
    return handle


def _find(root, window, name, role, automation_id, class_name, all):
    if role is not None and role not in ROLES:
        raise ValueError(f"Unknown role {role!r}; see controls.ROLES")
    handles = native.control_find(root, window, name or "", role or "", automation_id or "", class_name or "", all)
    return [Control(h) for h in handles]


def find(name=None, role=None, automation_id=None, class_name=None, window=None):
    """The first control that matches, searching the window in front, or
    `window`.  Returns a `Control`, or None.

        controls.find(name="OK", role="button")
        controls.find(automation_id="SearchBox")
        controls.find(name="*Save*", window=windows.find_one(process="notepad.exe"))
    """
    found = _find(0, _window_handle(window), name, role, automation_id, class_name, False)
    return found[0] if found else None


def find_all(name=None, role=None, automation_id=None, class_name=None, window=None):
    """Every control that matches, in the window in front or `window`."""
    return _find(0, _window_handle(window), name, role, automation_id, class_name, True)


def wait_for(name=None, role=None, automation_id=None, class_name=None, window=None, timeout=10.0):
    """Wait up to `timeout` seconds for a matching control to appear.
    Returns it, or None.  Without `window`, it looks in whichever window is
    in front each time."""
    waited = 0.0
    while True:
        found = find(name, role, automation_id, class_name, window)
        if found is not None or waited >= timeout:
            return found
        native.sleep(0.2)
        waited += 0.2


def focused():
    """The control with the keyboard focus, or None."""
    handle = native.control_focused()
    return Control(handle) if handle else None


def at(x, y):
    """The control at (x, y) on screen, or None."""
    handle = native.control_at(int(x), int(y))
    return Control(handle) if handle else None
