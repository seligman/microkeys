#!/usr/bin/env python3

import subprocess
import os


def run(cmd):
    print("> " + cmd)
    subprocess.check_call(cmd, shell=True)


def patch_wrapper(fix):
    fn = fix(None)
    print(f"Patching {fn}")
    run(f'git restore "{fn}"')
    with open(fn, "rt", newline="") as f:
        data = f.read()
    data = fix(data)
    with open(fn, "wt", newline="") as f:
        f.write(data)


def fix_project(data):
    if data is None:
        return r"ports\windows\micropython.vcxproj"

    data = data.replace(
        '<ConfigurationType>Application</ConfigurationType>',
        '<ConfigurationType>StaticLibrary</ConfigurationType>',
    )
    return data


def fix_main(data):
    if data is None:
        return r"ports\unix\main.c"

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
    return execute_from_lexer(LEX_SRC_STR, code, MP_PARSE_FILE_INPUT, true);
    return 0;
}
    """
    return data


def fix_print(data):
    if data is None:
        return r"py\modbuiltins.c"

    data = data.split("\n")
    for i, row in enumerate(data):
        if "mp_obj_t mp_builtin_print" in row:
            data = data[:i+1] + [
                "// TODO: Fix print handling",
                "return mp_const_none;",
            ] + data[i+1:]
    data = "\n".join(data)
    return data


def main():
    if not os.path.isdir("micropython"):
        print("MicroPython folder doesn't exist, grabbing...")
        run("git clone git@github.com:micropython/micropython.git")
        os.chdir("micropython")
        run("git checkout tags/v1.17")
    else:
        print("Already have MicroPython")
        os.chdir("micropython")

    patch_wrapper(fix_main)
    patch_wrapper(fix_project)
    patch_wrapper(fix_print)


if __name__ == "__main__":
    main()
