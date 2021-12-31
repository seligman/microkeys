import keys

@keys.key("test")
def test_func():
    keys.press("+abc%(123)~{up}")
    keys.press_raw("+{!")
    keys.press_raw("simple test")
