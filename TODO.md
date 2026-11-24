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

Done for 1.1:

- [x] An installable stub package (`pip install -e`) for editors and plain Python
- [x] Register and unregister bindings from code; `@on_start` and `@on_reload`
- [x] Window events: foreground change, window opened
- [x] Timers: `every()`, `at()`, `after()`
- [x] Clipboard-changed events and clipboard history
- [x] Rich clipboard: HTML, RTF, file lists, images, save and restore
- [x] Log window with clickable tracebacks
- [x] Python prompt in the log window, and `debugpy` attach
- [x] `store`: a dict saved between runs
- [x] `pip install` into a MicroKeys-managed `site-packages`
- [x] `ui.show()`: a message on screen while a macro works, or to say it's done
- [x] Builds without the updater (`make_release.py --no-updates`)

Done for 1.1.1:

- [x] Typing holds each key down (`key_hold_ms`, 15 ms), with precise waits between keys; Windows 11's Notepad loses keys that come straight back up
- [x] Programs that load DirectInput get keys lasting at least a frame (`directinput_key_ms`, 40 ms), and characters typed with their keys; `directinput_programs` overrides the detection per program
- [x] Before each key goes down, typing waits (up to a second) for the program in front to get back to its messages, so keys don't pile up while it's busy
- [x] A Ctrl+Alt hotkey lets go of Ctrl and presses it again while Alt is still down; otherwise when Alt is let go first, Windows 11's Notepad shows its access keys and the macro's typing drives its menus

Done for 1.2:

- [x] Key remapping (`keys.remap()`), and mouse buttons and wheel turns as hotkeys
- [x] Messages between machines (`net.send()` and `@net.on()`), found through DNS (MagicDNS) or Bonjour, paired by a checked token, signed with HMAC-SHA256
- [x] Screen: pixel colour, screenshots, finding and waiting for an image (`screen`)
- [x] Controls in windows through UI Automation (`controls`)
- [x] Finding and clicking text on screen, through accessibility or OCR (`screen.find_text`)
- [x] Export and import scripts and settings as one zip, with a preview, merge or replace, and a backup first
- [x] Run as administrator, with a scheduled task to start that way at sign-in
- [x] `ui.choose_file()`, and `Window.elevated`

Later:

- [ ] The macOS port (including AX for `controls`)
