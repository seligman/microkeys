#!/usr/bin/env python3

import json
import os
import re

KEYS = """
{"id": "08", "const": "BACK", "help": "The backspace key.", "desc": "Back"}
{"id": "09", "const": "TAB", "help": "The tab key.", "desc": "Tab"}
{"id": "0C", "const": "CLEAR", "help": "The 'Clear' key, or numpad '5'.", "desc": "Clear"}
{"id": "0D", "const": ["ENTER", "RETURN"], "help": "The enter/return key.", "desc": "Enter"}
{"id": "1B", "const": ["ESC", "ESCAPE"], "help": "The escape key.", "desc": "Escape"}
{"id": "20", "const": "SPACE", "help": "The space bar.", "desc": "Space"}
{"id": "23", "const": "END", "help": "The end key.", "desc": "End"}
{"id": "24", "const": "HOME", "help": "The home key.", "desc": "Home"}
{"id": "25", "const": "LEFT", "help": "The left arrow key.", "desc": "Left"}
{"id": "26", "const": "UP", "help": "The up arrow key.", "desc": "Up"}
{"id": "27", "const": "RIGHT", "help": "The right arrow key.", "desc": "Right"}
{"id": "28", "const": "DOWN", "help": "The down arrow key.", "desc": "Down"}
{"id": "2D", "const": ["INS", "INSERT"], "help": "The insert key.", "desc": "Insert"}
{"id": "2E", "const": ["DEL", "DELETE"], "help": "The delete key.", "desc": "Delete"}

{"id": "100", "const": "ALT", "help": "The alt modifier key.", "desc": "Alt", "modifier": true}
{"id": "200", "const": ["CONTROL", "CTRL"], "help": "The control modifier key.", "desc": "Control", "modifier": true}
{"id": "400", "const": "SHIFT", "help": "The shift modifier key.", "desc": "Shift", "modifier": true}
{"id": "800", "const": ["WIN", "WINDOWS"], "help": "The Windows modifier key.", "desc": "Windows", "modifier": true}

{"id": "30", "const": "0"}
{"id": "31", "const": "1"}
{"id": "32", "const": "2"}
{"id": "33", "const": "3"}
{"id": "34", "const": "4"}
{"id": "35", "const": "5"}
{"id": "36", "const": "6"}
{"id": "37", "const": "7"}
{"id": "38", "const": "8"}
{"id": "39", "const": "9"}

{"id": "41", "const": "A"}
{"id": "42", "const": "B"}
{"id": "43", "const": "C"}
{"id": "44", "const": "D"}
{"id": "45", "const": "E"}
{"id": "46", "const": "F"}
{"id": "47", "const": "G"}
{"id": "48", "const": "H"}
{"id": "49", "const": "I"}
{"id": "4A", "const": "J"}
{"id": "4B", "const": "K"}
{"id": "4C", "const": "L"}
{"id": "4D", "const": "M"}
{"id": "4E", "const": "N"}
{"id": "4F", "const": "O"}
{"id": "50", "const": "P"}
{"id": "51", "const": "Q"}
{"id": "52", "const": "R"}
{"id": "53", "const": "S"}
{"id": "54", "const": "T"}
{"id": "55", "const": "U"}
{"id": "56", "const": "V"}
{"id": "57", "const": "W"}
{"id": "58", "const": "X"}
{"id": "59", "const": "Y"}
{"id": "5A", "const": "Z"}

{"id": "70", "const": "F1"}
{"id": "71", "const": "F2"}
{"id": "72", "const": "F3"}
{"id": "73", "const": "F4"}
{"id": "74", "const": "F5"}
{"id": "75", "const": "F6"}
{"id": "76", "const": "F7"}
{"id": "77", "const": "F8"}
{"id": "78", "const": "F9"}
{"id": "79", "const": "F10"}
{"id": "7A", "const": "F11"}
{"id": "7B", "const": "F12"}
"""

def enum_const(val):
    if isinstance(val, str):
        return [val]
    else:
        return val

def handle_py_module_impl(keys):
    if keys is None:
        return "src/py_module_impl.cpp"
    ret = []
    for key in keys:
        if key.get("modifier", False):
            ret.append(f'if ((vk & 0x{key["id"]}) > 0) {{ AddDesc(desc, "{key["desc"]}"); }}')
    ret.append('switch (vk & 0xFF) {')
    for key in keys:
        if not key.get("modifier", False):
            ret.append(f'case 0x{key["id"]}: AddDesc(desc, "{key["desc"]}"); break;')
    ret.append('default: AddDesc(desc, "<Unknown>"); break;')
    ret.append('}')
    return ret

def handle_py_module(keys):
    if keys is None:
        return "src/py_module.c"
    ret = []
    for key in keys:
        for const in enum_const(key['const']):
            ret.append(f'{{ MP_ROM_QSTR(MP_QSTR_KEY_{const}), MP_ROM_INT(0x{key["id"]}) }},')
    return ret

def handle_keys_module(keys):
    if keys is None:
        return "macro/keys/__init__.py"
    ret = []
    for key in keys:
        for const in enum_const(key['const']):
            ret.append(f'KEY_{const} = 0x{key["id"]}')
            ret.append(f'"""{key["help"]}"""')
    return ret

def main():
    keys = []
    for cur in KEYS.split("\n"):
        cur = cur.strip()
        if len(cur):
            key = json.loads(cur)
            if "desc" not in key:
                key["desc"] = key["const"]
            if "help" not in key:
                key["help"] = f"The '{key['desc']}' key."
            keys.append(key)

    all_ids = set()
    all_consts = set()
    for key in keys:
        if key['id'] in all_ids:
            raise Exception(f"Key ID {key['id']} used more than once!")
        all_ids.add(key['id'])
        for const in enum_const(key['const']):
            if const in all_consts:
                raise Exception(f"Key const {const} used more than once!")
            all_consts.add(const)

    helpers = [
        handle_py_module_impl,
        handle_py_module,
        handle_keys_module,
    ]
    for helper in helpers:
        fn = helper(None)
        fn = fn.split("/")
        fn = os.path.join(*fn)
        sections = [[]]
        with open(fn, "rt") as f:
            for row in f:
                row = row.strip("\r\n")
                if "End of generated section -- Do not edit!" in row:
                    sections.append([])
                sections[-1].append(row)
                if "This section is generated by create_key_constants.py, do not edit by hand" in row:
                    sections.append([])
        if len(sections) != 3:
            raise Exception("Unable to find auto generated section in " + fn)
        indent = re.search("^([\t ]*)", sections[1][0]).group(1)
        replace = helper(keys)
        replace = [indent + x for x in replace]
        
        if "\n".join(replace) != "\n".join(sections[1]):
            with open(fn, "wt") as f:
                f.write("\n".join(sections[0] + replace + sections[2]) + "\n")
            print("Updated " + fn)
        else:
            print("No need to update " + fn)

if __name__ == "__main__":
    main()
