# MicroKeys

![](images/quick_run.gif)

This is a *work in progress*.

This is a very basic Windows key macro program that runs Python functions in response to key presses, using MicroPython to parse the script.  The long term goal is complete the macro language and improve the UI.

To run the POC, sync this project down, then run `get_micropython.py` to sync down MicroPython and patch it to be built as a library in a windows GUI app.

There is much work necessary to get this even to an Alpha, not the least of which:

- Add features to `keys.press`, like ctrl, shift, alt, ~ for enter, etc
- Implement `mouse.click` and such for some basic mouse features.
- Hook up exception handling to log errors.
- Log stdout/stderr to the application itself.
- Some memory leak testing, fixing of all the bugs.
- Improve UI.
- Documentation, including a mock `keys` package to make development easier.
- Make the "embed as a library" changes to MicroPython something that can be merged back into that project.
- Other functions as necessary to ease macros.

Questions? Feedback? [E-mail me](mailto:scott.seligman@gmail.com).
