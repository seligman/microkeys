import keys, windows

@keys.key("test")
def test_func():
    x = windows.get_active()
    print(f"Active: {x}")
    for handle, title, classname in windows.list_all():
        print(f'Handle: {handle}, Title: {title}, Class Name: {classname}')
    windows.set_active(handle)

    windows.launch("notepad")
    x, y, width, height = windows.get_position(handle)
    print(f"X: {x}, Y: {y}, Width: {width}, Height: {height}")
    windows.set_position(handle, 10, 10, 100, 100)
