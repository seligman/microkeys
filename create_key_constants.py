#!/usr/bin/env python3

"""
This module is responsible for updating py_module_impl.cpp, py_module.c, and 
the python keys module with all of the keys listed below in the JSONL data 
segment.  It is not run as part of a build process, but rather run on 
demand when necessary.
"""

KEYS = """
{"id": "08", "const": "BACK", "help": "The backspace key.", "desc": "Back"}
{"id": "09", "const": "TAB", "help": "The tab key.", "desc": "Tab"}
{"id": "0C", "const": "CLEAR", "help": "The 'clear' key, or numpad '5'.", "desc": "Clear"}
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

{"id": "30", "const": "#", "help": "The '#' key.", "desc": "#", "range": "0123456789"}
{"id": "41", "const": "#", "help": "The '#' key.", "desc": "#", "range": "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}
{"id": "70", "const": "#", "help": "The # function key.", "desc": "#", "range": ["F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"]}
"""

import json
import os
import re

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
            for val in key.get("range", [None]):
                if val is not None:
                    for prop in ['desc', 'const', 'help']:
                        key[prop] = key[prop].replace("#", val)
                keys.append(key)
                key = key.copy()
                next_id = f'{int(key["id"], 16)+1:02X}'
                key = json.loads(cur)
                key["id"] = next_id

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