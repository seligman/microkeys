import keys

@keys.key(keys.KEY_ALT | keys.KEY_Z)
def example_func():
    val = keys.add_ints(1, 2)
    val += 100 * val
    keys.press(f'MicroPython sends {val}!')
