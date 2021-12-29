import keys, clip

@keys.key(keys.KEY_ALT | keys.KEY_Z)
def example_func():
    x = clip.copy()
    result = eval(x)
    keys.press(f"{x} = {result}")

@keys.key("no-key-macro")
def example_no_key():
    # This can be called by running "MicroKeys.exe no-key-macro"
    # once MicroKeys.exe is up and running.
    keys.press("This is a macro triggered from a direct call")
