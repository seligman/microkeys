#!/usr/bin/env python

BUILD_EXE = True
INCLUDE_VIRUSTOTAL = True
PUSH_TO_GITHUB = True
GITHUB_PRERELEASE = True
GITHUB_REPO = "seligman/microkeys"
ZIP_NAME = "MicroKeys.zip"

import subprocess
import os
import random
from urllib.request import urlopen, Request
from urllib.parse import urlencode
import json
import re
from datetime import datetime
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
    make_multipart(ZIP_NAME, data, req)
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

def update_ver(changed):
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
        changed.append(os.path.join("src", "version.h"))
    
    return f'{new_ver[0]}.{new_ver[1]}.{new_ver[2]}'

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
    if BUILD_EXE:
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
    zip_name = os.path.join("dist", ZIP_NAME)
    with ZipFile(zip_name, 'w') as zipf:
        for fn, pn in get_dist_files():
            zipf.write(fn, pn)
    print("Created " + zip_name)
    with open(zip_name, "rb") as f:
        return f.read()

def verify_file_exists(fn, msg):
    fn = fn.split("/")
    fn = os.path.join(*fn)
    fn = os.path.expanduser(fn)
    if not os.path.isfile(fn):
        print(f"ERROR: {fn} not found, need {msg}")
        exit(1)

def push_to_github(ver, changed, notes, release_data):
    if len(changed) > 0:
        for cur in changed:
            run(f'git add "{cur}"')
        run(f'git commit -m "Release v{ver}"')
        run("git push")
    
    run(f'git tag v{ver}')
    run(f'git push origin v{ver}')

    with open(os.path.expanduser(os.path.join("~", ".github_token"))) as f:
        git_token = f.read().strip()

    data = {
        "tag_name": f"v{ver}",
        "name": f"release_{ver}",
        "prerelease": GITHUB_PRERELEASE,
        "body": "\n".join(notes),
    }
    headers = {
        "Authorization": f"token {git_token}",
    }
    data = json.dumps(data).encode("utf-8")

    print("Creating new github release")
    url = f"https://api.github.com/repos/{GITHUB_REPO}/releases"
    req = Request(url, headers=headers)
    try:
        resp = urlopen(req, data)
    except:
        for key, value in headers:
            print(f"{key}: {value}")
        print(f"URL: {url}")
        raise
    resp = json.loads(resp.read())
    upload_url = resp["upload_url"]
    upload_url = upload_url.split("{")[0]

    print("Uploading build artifact")
    headers = {
        "Authorization": f"token {git_token}",
        "Content-Type": "application/zip",
    }
    url = upload_url + "?" + urlencode({"name": ZIP_NAME, "label": ZIP_NAME})
    req = Request(url, headers=headers)
    try:
        urlopen(req, release_data)
    except:
        for key, value in headers:
            print(f"{key}: {value}")
        print(f"URL: {url}")
        raise

    print("All done")

def main():
    if INCLUDE_VIRUSTOTAL:
        verify_file_exists("~/.vt_key", "VirusTotal key")
    if PUSH_TO_GITHUB:
        verify_file_exists("~/.github_token", "GitHub token")

    changed = []
    ver = update_ver(changed)
    if BUILD_EXE:
        run("python3 get_micropython.py")
        build_exe()
    data = make_zip()
    if INCLUDE_VIRUSTOTAL:
        vt_url = get_vt_url(data)

    hashes = [
        (hashlib.md5(), "MD5"),
        (hashlib.sha256(), "SHA-256"),
        (hashlib.sha3_256(), "SHA-3"),
    ]
    for hash, _ in hashes:
        hash.update(data)

    notes = []
    note = input("Release note: ")
    if len(note) > 0:
        notes.append(note)
        notes.append("")

    if INCLUDE_VIRUSTOTAL:
        notes.append(f"Virus Total: [Results]({vt_url})")
    for hash, name in hashes:
        notes.append(f"{name}: `{hash.hexdigest()}`")

    print("-" * 80)
    for cur in notes:
        print(cur)
    print("-" * 80)

    if PUSH_TO_GITHUB:
        push_to_github(ver, changed, notes, data)

if __name__ == "__main__":
    main()
