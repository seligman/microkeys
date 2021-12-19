# MicroKeys

This is a *work in progress*.

Right now this is a very simple proof-of-concept of embedding MicroPython in a Win32 GUI application.

The long term goal is to use MicroPython as the engine for a key macro program.

To run the POC, sync this project down, then run `get_micropython.py` to sync down MicroPython and patch it to be built as a library in a windows GUI app.

There is much work necessary to get this even to an Alpha, not the least of which:

- [x] Find and remove dependencies on the Console, so this can run as a pure GUI application.
- [x] Allow Python code to call back into C++, to enable helpers like `type_string` and so on.
- [x] Add decorator so we know which functions to call for key presses.
- [x] Hook up decorator to allow more than one function.
- [x] Decorator should use VK_* style values instead of a string.
- [x] Load code from .py files.
- [ ] Show what keys are being registered
- [ ] Keep machine state around between keys.
- [ ] Properly implement `keys.press` to press keys.
- [ ] Hook up exception handling to log errors.
- [ ] Log stdout/stderr to the application itself.
- [ ] Some memory leak testing, fixing of all the bugs.
- [ ] Improve UI.
- [ ] Documentation.
- [ ] Make the "embed as a library" changes to MicroPython something that can be merged back into that project.
- [ ] Clipboard helper functions of some sort.
- [ ] Other functions as necessary to ease macros.
