#!/usr/bin/env python3

# Record images/quick_run.gif: run MicroKeys with demo/MicroKeys.py, run the
# Script app's demo, capture that part of the screen, and turn it into a GIF.
#
# Needs an unlocked desktop (not an SSH session), Python with mss and Pillow,
# a build of MicroKeys, and the Script app built with
# "dotnet build -c Release" in this folder.
#
# Usage: record_demo.py --exe <MicroKeysApp.exe> [--out <gif>]

import argparse
import ctypes
import json
import os
import shutil
import subprocess
import tempfile
import time

import mss
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")

# The part of the screen the Script app lays its windows out in
REGION = {"left": 0, "top": 22, "width": 1000, "height": 563}
FRAME_MS = 100
WM_COMMAND = 0x0111
IDM_EXIT = 105
EXPECTED = "(100 + 11) * 9 = 999"
RESULT = os.path.join(tempfile.gettempdir(), "microkeys_demo_result.txt")


def wait_for(cond, timeout, what):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return
        time.sleep(0.1)
    raise SystemExit(f"Timed out waiting for {what}")


def backdrop_up(frame):
    # The Script app's black background shows in all three outer corners;
    # once it's gone, the frame would show whatever is on the desktop
    w, h = frame.size
    return all(frame.getpixel(xy) == (0, 0, 0) for xy in ((2, 2), (w - 3, 2), (2, h - 3)))


def capture(script):
    # Grab the region every FRAME_MS until the Script app closes, keeping
    # only frames with the backdrop up
    frames = []
    with mss.mss() as sct:
        next_at = time.time()
        while script.poll() is None:
            shot = sct.grab(REGION)
            frame = Image.frombytes("RGB", shot.size, shot.rgb)
            if backdrop_up(frame):
                frames.append(frame)
            next_at += FRAME_MS / 1000
            time.sleep(max(0, next_at - time.time()))
    return frames


def write_gif(frames, out):
    # Merge runs of identical frames, then share one palette across them all
    merged = []
    for frame in frames:
        if merged and merged[-1][0].tobytes() == frame.tobytes():
            merged[-1][1] += FRAME_MS
        else:
            merged.append([frame, FRAME_MS])
    palette = merged[-1][0].quantize(colors=128, method=Image.Quantize.MEDIANCUT)
    images = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f, _ in merged]
    images[0].save(out, save_all=True, append_images=images[1:], duration=[d for _, d in merged], loop=0, optimize=True)
    print(f"Wrote {out}: {len(images)} frames, {sum(d for _, d in merged) / 1000:.1f} seconds, {os.path.getsize(out):,} bytes")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, help="MicroKeysApp.exe to record")
    parser.add_argument("--script", default=os.path.join(HERE, "bin", "Release", "net10.0-windows", "Script.exe"))
    parser.add_argument("--out", default=os.path.join(ROOT, "images", "quick_run.gif"))
    args = parser.parse_args()

    # A throwaway script folder and settings, so the recording doesn't touch
    # the real ones, with slower typing so the answer is visible as it's typed
    work = os.path.join(ROOT, "build", "demo")
    if os.path.isdir(work):
        shutil.rmtree(work)
    scripts = os.path.join(work, "scripts")
    data = os.path.join(work, "data")
    os.makedirs(scripts)
    os.makedirs(data)
    shutil.copy(os.path.join(HERE, "demo", "MicroKeys.py"), scripts)
    with open(os.path.join(data, "settings.json"), "w") as f:
        json.dump({"key_delay_ms": 40}, f)

    user32 = ctypes.windll.user32
    if user32.FindWindowW("MicroKeysMainWindow", None):
        raise SystemExit("MicroKeys is already running; exit it first")
    micro = subprocess.Popen([args.exe, "--scripts", scripts, "--data", data])
    log = os.path.join(data, "MicroKeys.log")
    wait_for(lambda: os.path.isfile(log) and "Loaded" in open(log, encoding="utf-8").read(), 20, "MicroKeys to load the demo script")

    if os.path.isfile(RESULT):
        os.unlink(RESULT)
    script = subprocess.Popen([args.script, "--demo"], cwd=HERE)
    # The Script app spends its first two seconds laying out its windows
    time.sleep(2.0)
    frames = capture(script)

    user32.PostMessageW(user32.FindWindowW("MicroKeysMainWindow", None), WM_COMMAND, IDM_EXIT, 0)
    micro.wait(timeout=10)

    # Keys typed into the wrong window make a GIF of nothing happening, so check
    result = open(RESULT, encoding="utf-8").read() if os.path.isfile(RESULT) else None
    if result != EXPECTED:
        raise SystemExit(f"The demo ended with {result!r} in the text box instead of {EXPECTED!r}; "
                         "did another window hold the keyboard focus?  No GIF written.")
    if not frames:
        raise SystemExit("No frames had the Script app's backdrop up; is the region right?  No GIF written.")
    write_gif(frames, args.out)


if __name__ == "__main__":
    main()
