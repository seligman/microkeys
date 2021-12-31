#!/usr/bin/env python3

import subprocess
import os
import sys
import re

os.chdir(os.path.split(__file__)[0])

if len(sys.argv) != 2:
    print("Need to specify Debug or Release to run")
    exit(1)


exe = os.path.join("..", "x64", sys.argv[1], "MicroKeys.exe")
if not os.path.isfile(exe):
    print("ERROR: Unable to find " + exe)

print("Running tests...")
good = 0
for cur in sorted(os.listdir(".")):
    if cur.startswith("test_") and cur.endswith(".py"):
        expected = cur[:-3] + ".txt"

        tests = []
        with open(cur) as f:
            for row in f:
                m = re.search('@keys.key\\("(?P<test>.*)"\\)', row)
                if m is not None:
                    tests.append(m.group("test"))

        temp = "temp_output.txt"
        if os.path.isfile(temp):
            os.unlink(temp)

        if len(tests) == 0:
            raise Exception(f"Unable to find tests in {cur}")

        os.environ["MICROKEYS_LOG"] = temp
        os.environ["MICROKEYS_RUN"] = ",".join(tests)
        os.environ["MICROKEYS_SOURCE"] = cur

        try:
            subprocess.check_call([exe])
        except:
            for key, value in os.environ.items():
                if key.startswith("MICROKEYS_"):
                    print(f"{key}={value}")
            raise

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
        good += 1
        print(f"{good:3d}: {cur[5:-3]} is good.")

print(f"All {good} tests are good")
