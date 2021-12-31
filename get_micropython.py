#!/usr/bin/env python3

import subprocess
import os

TARGET_TAG="tags/v1.17"


def run(cmd):
    print("> " + cmd)
    subprocess.check_call(cmd, shell=True)


def patch_wrapper(fix):
    fn = fix(None)

    data = subprocess.check_output(["git", "show", TARGET_TAG + ":" + fn])
    data = data.decode("utf-8")
    data = data.replace("\r\n", "\n")
    data = fix(data)

    fn = fn.split("/")
    fn = os.path.join(*fn)

    with open(fn, "rt", newline="", encoding="utf-8") as f:
        on_disc = f.read()

    if on_disc == data:
        print(f"Skipping {fn}, already up to date")
    else:
        print(f"Patching {fn}")
        with open(fn, "wt", newline="", encoding="utf-8") as f:
            f.write(data)


def fix_project(data):
    if data is None:
        return "ports/windows/micropython.vcxproj"

    data = data.replace(
        '<ConfigurationType>Application</ConfigurationType>',
        '<ConfigurationType>StaticLibrary</ConfigurationType>',
    )

    data = data.split("\n")
    todo = []
    done = {}
    for i, line in enumerate(data):
        if "<ItemGroup>" in line and not done.get("py_module", False):
            done["py_module"] = True
            todo.append((i + 1, r'    <ClCompile Include="..\..\..\src\py_module.c" />'))
    todo.sort(reverse=True)
    for i, line in todo:
        data.insert(i, line)
    data = "\r\n".join(data)

    return data


def fix_main(data):
    if data is None:
        return "ports/unix/main.c"

    data = data.replace("int main(", "int old_main(")
    data += "\n" + """
int run_micro_python(const char* code) {
    mp_stack_ctrl_init();
    char* heap = malloc(heap_size);
    gc_init(heap, heap + heap_size);
    mp_init();
    mp_uint_t stack_limit = 40000 * (sizeof(void*) / 4);
    mp_stack_set_limit(stack_limit);
    static mp_obj_t pystack[1024];
    mp_pystack_init(pystack, &pystack[MP_ARRAY_SIZE(pystack)]);
    return execute_from_lexer(LEX_SRC_STR, code, MP_PARSE_FILE_INPUT, true);
}

int run_fun(void* fun) {
    mp_stack_ctrl_init();
    mp_hal_set_interrupt_char(CHAR_CTRL_C);

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_call_function_0((mp_obj_t)fun);
        mp_hal_set_interrupt_char(-1);
        mp_handle_pending(true);
        nlr_pop();
    }
    return 0;
}

char* get_micropython_ver() {
    return MICROPY_VERSION_STRING;
}
    """.strip() + "\n"
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
        run("git checkout " + TARGET_TAG)
    else:
        print("Already have MicroPython")
        os.chdir("micropython")

    patch_wrapper(fix_main)
    patch_wrapper(fix_project)
    patch_wrapper(fix_print)
    patch_wrapper(fix_objfun)


if __name__ == "__main__":
    main()
