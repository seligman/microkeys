#!/usr/bin/env python3

# Recreate expected output for the limits test, so we don't need to 
# check in a large data file

big = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" * 200
need_shift = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
shift = False

for func in ["press_raw", "press"]:
    print(f'keys.{func}("{big}")')
    for cur in list(big) + [None]:
        want_shift = cur in need_shift
        if want_shift != shift:
            if want_shift:
                print("  Key: Shift down")
            else:
                print("  Key: Shift up")
            shift = want_shift
        if cur is not None:
            print(f'  Key: {cur.upper()} press')

print(f'keys.log("{big}")')

print(f'clip.paste("{big}")')
print(f'clip.copy()')
print(f'STDOUT: {big}')

print(f'windows.launch("{big}")')

print("STDOUT: Test lmits is done")
