#!/usr/bin/env python3

import subprocess
import os
import shutil
import re

def clean_html(fn, replace_links=False):
    with open(fn) as f:
        data = f.read()
    data = re.sub('<font color="#ffffff" face="helvetica, arial"><a.*?</font>', '', data)
    if replace_links:
        data = data.replace('"macro.', '"')
        data = data.replace('<td width="25%" valign=top><a href="MicroKeys.html">MicroKeys</a><br>\n</td>', '')
    with open(fn, "w") as f:
        f.write(data)

def get_rel_dir(*args):
    args = [os.path.split(__file__)[0]] + list(args)
    return os.path.join(*args)

def move(source_fn, dest_dir):
    dest_fn = os.path.join(dest_dir, os.path.split(source_fn)[1])
    if os.path.isfile(dest_fn):
        os.unlink(dest_fn)
    shutil.move(source_fn, dest_fn)

def main():
    dest_dir = get_rel_dir("output")
    if not os.path.isdir(dest_dir):
        os.mkdir(dest_dir)
    for cur in os.listdir(dest_dir):
        os.unlink(os.path.join(dest_dir, cur))

    os.chdir(get_rel_dir(".."))
    subprocess.check_call(["python3", "-m", "pydoc", "-w", "macro"])
    clean_html("macro.html", replace_links=True)
    move("macro.html", dest_dir)
    os.rename(os.path.join(dest_dir, "macro.html"), os.path.join(dest_dir, "index.html"))

    os.chdir(get_rel_dir("..", "macro"))
    for cur in os.listdir("."):
        if os.path.isdir(cur) and os.path.isfile(os.path.join(cur, "__init__.py")):
            subprocess.check_call(["python3", "-m", "pydoc", "-w", cur])
            clean_html(cur + ".html")
            move(cur + ".html", dest_dir)

if __name__ == "__main__":
    main()
