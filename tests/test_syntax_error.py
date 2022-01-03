import keys

# This will just verify that a syntax error bubbles up to the log correctly

@keys.key("test")
def test_func():
    this = is a syntax error and shouldnt compile
    print("If we get this far, something is wrong")
