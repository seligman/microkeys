#!/usr/bin/env python

import subprocess
import os
import random
from urllib.request import urlopen, Request
import json
import re
from datetime import datetime, timedelta
from zipfile import ZipFile
import hashlib
import time

def get_vt_api_key():
    fn = os.path.expanduser(os.path.join("~", ".vt_key"))
    with open(fn) as f:
        return f.read().strip()

def make_multipart(filename, data, req):
    while True:
        boundary = ''.join(random.choice('0123456789abcdef') for n in range(60))
        if not boundary.encode("utf-8") in data:
            break
    header = f"--{boundary}\r\n"
    header += f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
    header += '\r\n'
    footer = f"\r\n--{boundary}--\r\n"

    header = header.encode("utf-8")
    footer = footer.encode("utf-8")

    req.data = header + data + footer
    req.headers["Content-Length"] = str(len(req.data))
    req.headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"

def get_vt_url(data):
    print("Getting upload URL...")
    req = Request(
        "https://www.virustotal.com/api/v3/files/upload_url", 
        headers={
            "x-apikey": get_vt_api_key(),
        },
    )
    resp = urlopen(req).read()
    resp = json.loads(resp)
    url = resp["data"]

    print("Uploading data...")
    req = Request(
        url, 
        headers={
            "x-apikey": get_vt_api_key(),
            'Accept': 'application/json',
        },
        method="POST",
    )
    make_multipart("MicroKeys.zip", data, req)
    resp = urlopen(req).read()
    resp = json.loads(resp)
    analysis = resp["data"]["id"]

    while True:
        req = Request(
            "https://www.virustotal.com/api/v3/analyses/" + analysis,
            headers={
                "x-apikey": get_vt_api_key(),
                'Accept': 'application/json',
            },
        )
        resp = urlopen(req).read()
        resp = json.loads(resp)
        if "meta" in resp and "file_info" in resp["meta"] and "sha256" in resp["meta"]["file_info"]:
            return "https://www.virustotal.com/gui/file/" + resp["meta"]["file_info"]["sha256"]
        else:
            print("Not ready yet, will try again...")
            time.sleep(5)

def update_ver():
    ver = [0, 0, 0]
    with open(os.path.join("src", "version.h")) as f:
        for row in f:
            m = re.search("VERSION_([123])[\t ]+([0-9]+)", row)
            if m is not None:
                ver[int(m.group(1)) - 1] = int(m.group(2))
    msg = f"Enter new version [{ver[0]}.{ver[1]}.{ver[2]}]: "
    new_ver = input(msg).strip()
    if len(new_ver) == 0:
        new_ver = f"{ver[0]}.{ver[1]}.{ver[2]}"
    
    ver = tuple(ver)
    new_ver = tuple(int(x) for x in new_ver.split("."))
    if len(new_ver) != 3:
        raise Exception("Unknown version syntax")
    if new_ver != ver:
        data = [
            f'#pragma once',
            f'',
            f'#define VERSION_1       {new_ver[0]}',
            f'#define VERSION_2       {new_ver[1]}',
            f'#define VERSION_3       {new_ver[2]}',
            f'#define VERSION_TEXT    "{new_ver[0]}.{new_ver[1]}.{new_ver[2]}"',
            f'#define VERSION_TEXT_T  _T(VERSION_TEXT)',
            f'#define VERSION_YEAR    "{datetime.now().strftime("%Y")}"',
            f'#define VERSION_YEAR_T  _T(VERSION_YEAR)',
        ]
        with open(os.path.join("src", "version.h"), "wt") as f:
            f.write("\n".join(data) + "\n")

def build_exe():
    devenv = r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\IDE\devenv.com"
    run([devenv, "/build", "Release", "MicroKeys.sln"])

def run(cmd):
    if isinstance(cmd, list):
        print("$ " + " ".join(f'"{x}"' if ' ' in x else x for x in cmd))
        subprocess.check_call(cmd)
    else:
        print("$ " + cmd)
        subprocess.check_call(cmd, shell=True)

def get_dist_files():
    yield os.path.join("x64", "Release", "MicroKeys.exe"), "MicroKeys.exe"
    dirs = [("macro", "")]
    while len(dirs) > 0:
        dirname, pretty = dirs.pop(0)
        for cur in os.listdir(dirname):
            if cur != "nytxw_puz.zip":
                fn = os.path.join(dirname, cur)
                pn = cur if len(pretty) == 0 else pretty + "/" + cur
                if os.path.isfile(fn):
                    if not fn.endswith(".md"):
                        yield fn, pn
                elif os.path.isdir(fn):
                    dirs.append((fn, pn))

def make_zip():
    if not os.path.isdir("dist"):
        os.mkdir("dist")
    zip_name = os.path.join("dist", "MicroKeys.zip")
    with ZipFile(zip_name, 'w') as zipf:
        for fn, pn in get_dist_files():
            zipf.write(fn, pn)
    print("Created " + zip_name)
    with open(zip_name, "rb") as f:
        return f.read()

def main():
    update_ver()
    run("python3 get_micropython.py")
    build_exe()
    data = make_zip()
    vt_url = get_vt_url(data)

    hashes = [
        (hashlib.md5(), "MD5"),
        (hashlib.sha256(), "SHA-256"),
        (hashlib.sha3_256(), "SHA-3"),
    ]
    for hash, _ in hashes:
        hash.update(data)

    print("-" * 80)
    print(f"Virus Total: [Results]({vt_url})")
    for hash, name in hashes:
        print(f"{name}: `{hash.hexdigest()}`")

if __name__ == "__main__":
    main()
