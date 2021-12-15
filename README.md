# MicroKeys

This is a *work in progress*.

Right now this is a very simple proof-of-concept of embedding MicroPython in a Win32 GUI application.

The long term goal is to use MicroPython as the engine for a macro program.

To run the POC, sync this project down, then run `get_micropython.py` to sync down MicroPython and patch it to be built as a library.

There is much work necessary to get this even to an Alpha, not the least of which:

- Find and remove dependencies on the Console, so this can run as a pure GUI application.
- Allow Python code to call back into C++, to enable helpers like `type_string` and so on.
- Load code from .py files, including some declaration language for what global keystrokes they should be invoked by.
- Make the "embed as a library" changes to MicroPython something that can be merged back into that project.
