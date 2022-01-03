import keys, clip

# Also test globals state in MicroPython 
# with a simple variable test
a = 1

@keys.key("test1")
def test_func_1():
    global a
    a += 1
    val = clip.copy()
    print(f"{val} {a}")

@keys.key("test2")
def test_func_2():
    global a
    a += 1
    val = clip.paste(f"{a}")

@keys.key("test3")
def test_func_3():
    global a
    a += 1
    val = clip.copy()
    print(f"{val} {a}")
