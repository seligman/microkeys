## Todo

Tracking the work after 1.0.  The plan and its priorities live in the productization plan doc.

Done for 1.0:

- [x] Embed CPython (3.14) instead of MicroPython, on a script thread of its own
- [x] Platform interface, with the Windows implementation behind it
- [x] CMake build, x64 and ARM64
- [x] Hotkeys written as strings, per-app hotkeys, hotstrings
- [x] Unicode typing, `paste_text`, window objects, `run` and `open`
- [x] `msgbox`, `input`, `choose`, and toasts
- [x] Tray app, abort hotkey, busy indicator, start with Windows
- [x] Script folder in Documents with a starter script, several scripts per folder, keep-last-good reloads
- [x] Per-monitor DPI awareness
- [x] Generated API docs, a guide, a cookbook, and editor autocomplete through pyrightconfig.json
- [x] Zip releases and a self-updater
- [x] Unit tests (core and Python package), headless integration tests, GUI tests, GitHub Actions

Next (P2):

- [ ] Register and unregister bindings from code; `@on_start` and `@on_reload`
- [ ] Window events: foreground change, window opened
- [ ] Timers: `every()`, `at()`, `after()`
- [ ] Clipboard-changed events and clipboard history
- [ ] Rich clipboard: HTML, RTF, file lists, images
- [ ] Log window with clickable tracebacks
- [ ] REPL console and `debugpy` attach
- [ ] `store`: a dict saved between runs
- [ ] `pip install` into a MicroKeys-managed `site-packages`

Later (P3):

- [ ] Key remapping and mouse-button triggers
- [ ] Messages between machines (`net.send()` and `@net.on()`, MagicDNS or Bonjour names, paired by token)
- [ ] Screen: pixel colour, screenshot, wait for an image
- [ ] UI Automation (Windows UIA, macOS AX)
- [ ] Export and import scripts and settings as one zip
- [ ] "Run elevated" option
- [ ] The macOS port
