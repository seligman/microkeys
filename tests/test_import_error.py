import keys
# Verify that an import error correctly bubbles up
import package_that_does_not_exist

@keys.key("test")
def test_func():
    print("If we get this far, something is wrong")
