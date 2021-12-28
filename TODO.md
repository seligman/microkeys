## Todo

Tracking the work items to get to different stages of development.

Items needed to get to an alpha release:

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
- [x] Clipboard helper functions like `clip.copy` and `clip.paste` of some sort.
- [ ] Add some window management features, like `win.switch`, `win.title`, and so on.
- [ ] Verify state is saved between calls.
- [ ] A CLI version to test out code quickly.
- [x] Documentation, perhaps built from the harness code.
- [x] A build and distribution that can be easily downloaded.

Items needed to get to a beta release:

- [ ] Make the "embed as a library" changes to MicroPython something that can be merged back into that project.
- [ ] GUI based input and output, such as `win.input('prompt?')` to get a string, and `win.msgbox('info')` to show some text.
- [ ] Documentation, including mock packages to make development easier.
- [ ] Add support and examples for parsing CSV and JSON strings.
- [ ] Hook up exception handling to log errors.
- [ ] Log stdout/stderr to the application itself.
- [ ] Some memory leak testing, fixing of all the bugs.
- [ ] Improve UI.
- [ ] A proper installer.

Items for future work:

- [ ] A macOS port.
- [ ] Something like a built in IDE to debug scripts.
