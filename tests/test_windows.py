import keys, clip, windows

@keys.key("test")
def test_func():
    x = windows.get_active()
    clip.paste(str(x))
    x = windows.list_all()
    clip.paste(str(x))
    windows.set_active(x[0][0])
