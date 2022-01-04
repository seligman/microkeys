## Todo

Tracking the work items to get to different stages of development.

Items needed to get past an alpha release:

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
- [x] Add features to `keys.press`, like ctrl, shift, alt, ~ for enter, etc
- [x] Implement `mouse.click` and such for some basic mouse features.
- [x] Clipboard helper functions like `clip.copy` and `clip.paste` of some sort.
- [x] Add some window management features, like `win.switch`, `win.title`, and so on.
- [x] Verify state is saved between calls.
- [x] A CLI version to test out code quickly.
- [x] Basic documentation for python modules.
- [x] A build and distribution that can be easily downloaded.
- [x] Allow keyless macros.

Items needed to get past a beta release:

- [ ] Look into issues reloading the source code not reloading functions
- [ ] Make the "embed as a library" changes to MicroPython something that can be merged back into that project.
- [ ] GUI based input and output, such as `win.input('prompt?')` to get a string, and `win.msgbox('info')` to show some text.
- [ ] Documentation, including mock packages to make development easier.
- [ ] Add support and examples for parsing CSV and JSON strings.
- [x] Hook up exception handling to log errors.
- [ ] Auto reload python script when the file changes.
- [x] Log stdout/stderr to the application itself.
- [ ] Some memory leak testing, fixing of all the bugs.
- [ ] Improve UI.
- [ ] A proper installer.
- [ ] Documentation, perhaps built from the harness code.
- [ ] Document the test environment variables.
- [ ] Add an option to minimize down to a notification icon.
- [ ] Some sort of REPL interface to debug and test changes
- [ ] A way for a function to register and deregister macros
- [ ] An option to trigger a function on window changes

Items for future work:

- [ ] A macOS port.
- [ ] Something like a built in IDE to debug scripts.
