#!/usr/bin/env python3

import subprocess
import os


def run(cmd):
    print("> " + cmd)
    subprocess.check_call(cmd, shell=True)


def patch_proj():
    fn = r"ports\windows\micropython.vcxproj"
    print(f"Patching {fn}")
    with open(fn, "rt", newline="") as f:
        data = f.read()

    if "StaticLibrary" in data:
        print("  Already patched!")
    else:
        data = data.replace(
            '<ConfigurationType>Application</ConfigurationType>',
            '<ConfigurationType>StaticLibrary</ConfigurationType>',
        )
        with open(fn, "wt", newline="") as f:
            f.write(data)

def patch_main():
    fn = r"ports\unix\main.c"
    print(f"Patching {fn}")
    with open(fn, "rt", newline="") as f:
        data = f.read()
    if "run_micro_python" in data:
        print("  Already patched!")
    else:
        data = data.replace("int main(", "int old_main(")
        data += """
int run_micro_python(const char* code) {
    // TODO
    mp_stack_ctrl_init();
    char* heap = malloc(heap_size);
    gc_init(heap, heap + heap_size);
    mp_init();
    mp_uint_t stack_limit = 40000 * (sizeof(void*) / 4);
    mp_stack_set_limit(stack_limit);
    static mp_obj_t pystack[1024];
    mp_pystack_init(pystack, &pystack[MP_ARRAY_SIZE(pystack)]);
    return execute_from_lexer(LEX_SRC_STR, code, MP_PARSE_EVAL_INPUT, true);
    return 0;
}
        """
        with open(fn, "wt", newline="") as f:
            f.write(data)


def main():
    if not os.path.isdir("micropython"):
        print("MicroPython folder doesn't exist, grabbing...")
        run("git clone git@github.com:micropython/micropython.git")
        os.chdir("micropython")
        run("git checkout tags/v1.17")
    else:
        print("Already have MicroPython")
        os.chdir("micropython")

    patch_main()
    patch_proj()


if __name__ == "__main__":
    main()
