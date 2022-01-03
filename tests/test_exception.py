import keys

@keys.key("test")
def test_func():
    print("Before exception")
    raise Exception("Test Exception")
    print("After exception")
