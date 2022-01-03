import keys, clip, windows

# Test the functions that take a string, to verify
# that they can deal with a large string safely

# The large string, 12400 bytes big
big = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" * 200

@keys.key("test")
def test_func():
    keys.press_raw(big)
    keys.press(big)
    keys.log(big)

    clip.paste(big)
    x = clip.copy()
    print(x)

    # Probably not a fair test, since CreateProcess will complain about long strings
    # but still include it to test the MicroPython -> MicroKeys interface
    windows.launch(big)

    # Add a simple end of test check to make sure the previous
    # command didn't cause the executable to crash
    print("Test lmits is done")
