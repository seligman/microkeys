import keys

@keys.key(keys.KEY_ALT | keys.KEY_Z)
def example_func():
    for i in range(10):
        keys.press(f"Line number {i}\n")
