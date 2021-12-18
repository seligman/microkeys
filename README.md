# MicroKeys

This is a *work in progress*.

Right now this is a very simple proof-of-concept of embedding MicroPython in a Win32 GUI application.

The long term goal is to use MicroPython as the engine for a macro program.

To run the POC, sync this project down, then run `get_micropython.py` to sync down MicroPython and patch it to be built as a library.

There is much work necessary to get this even to an Alpha, not the least of which:

- &#9745; Find and remove dependencies on the Console, so this can run as a pure GUI application.
- &#9745; Allow Python code to call back into C++, to enable helpers like `type_string` and so on.
- &#9744; Add annotations so we know which functions to call for key presses.
- &#9744; Load code from .py files.
- &#9744; Much better UI.
- &#9744; Documentation.
- &#9744; Make the "embed as a library" changes to MicroPython something that can be merged back into that project.
