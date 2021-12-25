# MicroKeys

![](images/quick_run.gif)

This is a *work in progress*.

This is a very basic Windows key macro program that runs Python functions in response to key presses, using MicroPython to parse the script.  The long term goal is complete the macro language and improve the UI.

To run the POC, sync this project down, then run `get_micropython.py` to sync down MicroPython and patch it to be built as a library in a windows GUI app.

There is much work necessary to get this even to an Alpha, not the least of which:

- [x] Find and remove dependencies on the Console, so this can run as a pure GUI application.
- [x] Allow Python code to call back into C++, to enable helpers like `type_string` and so on.
- [x] Add decorator so we know which functions to call for key presses.
- [x] Hook up decorator to allow more than one function.
- [x] Decorator should use VK_* style values instead of a string.
- [x] Load code from .py files.
- [x] Show what keys are being registered
- [x] Keep machine state around between keys.
- [x] Properly implement `keys.press` to press keys.
- [x] Don't stall out the GUI message loop while doing work.
- [ ] Add features to `keys.press`, like ctrl, shift, alt, ~ for enter, etc
- [ ] Implement `mouse.click` and such for some basic mouse features.
- [ ] Hook up exception handling to log errors.
- [ ] Log stdout/stderr to the application itself.
- [ ] Some memory leak testing, fixing of all the bugs.
- [ ] Improve UI.
- [ ] Documentation, including a mock `keys` package to make development easier.
- [ ] Make the "embed as a library" changes to MicroPython something that can be merged back into that project.
- [x] Clipboard helper functions like `clip.copy` and `clip.paste` of some sort.
- [ ] Other functions as necessary to ease macros.
