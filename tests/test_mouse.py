import keys, mouse

@keys.key("test")
def test_func():
    mouse.down()
    mouse.up()
    mouse.right_down()
    mouse.right_up()

    mouse.click()
    mouse.right_click()

    x, y = mouse.position()
    print(f"{x}, {y}")
    mouse.move(50, 60)
    x, y = mouse.position()
    print(f"{x}, {y}")
    mouse.offset(-25, -25)
    x, y = mouse.position()
    print(f"{x}, {y}")
