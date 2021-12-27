import keys, clip

@keys.key(keys.KEY_ALT | keys.KEY_Z)
def example_func():
    x = clip.copy()
    result = eval(x)
    keys.press(f"{x} = {result}")
