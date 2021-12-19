import keys

@keys.key('test')
def example_func():
    val = keys.add_ints(1, 2)
    val += 100
    keys.press(f'MicroPython sends {val}!')
