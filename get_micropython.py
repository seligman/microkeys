#!/usr/bin/env python3

import subprocess
import os


def run(cmd):
    print("> " + cmd)
    subprocess.check_call(cmd, shell=True)


def patch_wrapper(fix):
    fn = fix(None)
    fn = fn.split("/")
    fn = os.path.join(*fn)
    print(f"Patching {fn}")
    run(f'git restore "{fn}"')
    with open(fn, "rt", newline="") as f:
        data = f.read()
    data = fix(data)
    with open(fn, "wt", newline="") as f:
        f.write(data)


def fix_project(data):
    if data is None:
        return "ports/windows/micropython.vcxproj"

    data = data.replace(
        '<ConfigurationType>Application</ConfigurationType>',
        '<ConfigurationType>StaticLibrary</ConfigurationType>',
    )

    data = data.split("\r\n")
    todo = []
    done = {}
    for i, line in enumerate(data):
        if "<ItemGroup>" in line and not done.get("py_module", False):
            done["py_module"] = True
            todo.append((i + 1, '<ClCompile Include="..\..\..\py_module.c" />'))
    todo.sort(reverse=True)
    for i, line in todo:
        data.insert(i, line)
    data = "\r\n".join(data)

    return data


def fix_main(data):
    if data is None:
        return "ports/unix/main.c"

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

int run_fun(void* fun) {
    // TODO
    mp_call_function_0((mp_obj_t)fun);
    return 0;
}
    """
    return data


def fix_objfun(data):
    if data is None:
        return "py/objfun.c"

    todo = []
    data = data.split("\n")
    found = {}

    for i, line in enumerate(data):
        if "#if MICROPY_EMIT_NATIVE" in line:
            found["native"] = found.get("native", 0) + 1
            if found["native"] == 3:
                todo.append((i, "native"))

    todo.sort(reverse=True)
    for i, patch in todo:
        if patch == "native":
            data[i] = data[i].replace("#if MICROPY_EMIT_NATIVE", "#if MICROPY_EMIT_NATIVE || 1")

    return "\n".join(data)


def fix_print(data):
    if data is None:
        return "py/modbuiltins.c"

    data = data.split("\n")
    for i, row in enumerate(data):
        if "mp_obj_t mp_builtin_print" in row:
            data = data[:i+1] + [
                "// TODO: Fix print handling",
                "return mp_const_none;",
            ] + data[i+1:]
            break
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
    patch_wrapper(fix_objfun)


if __name__ == "__main__":
    main()
