#!/usr/bin/env python3

import subprocess
import os
import sys

os.chdir(os.path.split(__file__)[0])

if len(sys.argv) != 2:
    print("Need to specify Debug or Release to run")
    exit(1)


exe = os.path.join("..", "x64", sys.argv[1], "MicroKeys.exe")
if not os.path.isfile(exe):
    print("ERROR: Unable to find " + exe)

for cur in sorted(os.listdir(".")):
    if cur.startswith("test_") and cur.endswith(".py"):
        expected = cur[:-3] + ".txt"

        temp = "temp_output.txt"
        if os.path.isfile(temp):
            os.unlink(temp)

        os.environ["MICROKEYS_LOG"] = temp
        os.environ["MICROKEYS_RUN"] = "test"
        os.environ["MICROKEYS_SOURCE"] = cur

        subprocess.check_call([exe])

        if os.path.isfile(expected):
            with open(expected) as f:
                expected_data = f.read()
            with open(temp) as f:
                test_data = f.read()
            if test_data != expected_data:
                print(f"ERROR: {cur} did not complete properly!")
                print(f"Compare {temp} to {expected}")
                exit(1)
        else:
            print(f"WARNING: {cur} does not have test results")
            print(f"Check {temp}, and if it's good, rename it to {expected}")
            exit(1)
        
        os.unlink(temp)
        print(f"{cur} is good.")

print("All done!")
