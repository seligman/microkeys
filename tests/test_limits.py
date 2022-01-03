import keys

# Test the functions that take a string, to verify
# that they can deal with a large string safely

# The large string, 12400 bytes big
big = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" * 2
# TODO: This should be '* 200', not '* 2', along with the related
# string in test_limits.expected.py

@keys.key("test")
def test_func():
    keys.press_raw(big)
    keys.press(big)
    # TODO: Test other things that take a string
    # Add a simple end of test check to make sure the previous
    # command didn't cause the executable to crash
    print("Test lmits is done")
