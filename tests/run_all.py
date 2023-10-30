#!/usr/bin/env python3

import subprocess
import os
import sys
import re
from datetime import datetime
if sys.version_info >= (3, 11): from datetime import UTC
else: import datetime as datetime_fix; UTC=datetime_fix.timezone.utc

def main(release_type, target_tests):
    os.chdir(os.path.split(__file__)[0])

    exe = os.path.join("..", "x64", release_type, "MicroKeys.exe")
    if not os.path.isfile(exe):
        print("ERROR: Unable to find " + exe)
        exit(1)

    print("Running tests...")
    good = 0
    for cur in sorted(os.listdir(".")):
        if cur.startswith("test_") and cur.endswith(".py") and not cur.endswith(".expected.py"):
            run_test = True
            if target_tests is not None:
                run_test = False
                if cur[5:-3].lower() in target_tests:
                    run_test = True

            if run_test:
                expected = cur[:-3] + ".txt"
                expected_py = cur[:-3] + ".expected.py"

                tests = []
                with open(cur) as f:
                    for row in f:
                        m = re.search('@keys.key\\("(?P<test>.*)"\\)', row)
                        if m is not None:
                            if "," in m.group("test"):
                                raise Exception(f"Tests names can not contain commas, see '{m.group('test')}' in {cur}")
                            tests.append(m.group("test"))

                temp = "temp_output.txt"
                if os.path.isfile(temp):
                    os.unlink(temp)

                if len(tests) == 0:
                    raise Exception(f"Unable to find tests in {cur}")

                os.environ["MICROKEYS_LOG"] = temp
                os.environ["MICROKEYS_RUN"] = ",".join(tests)
                os.environ["MICROKEYS_SOURCE"] = cur

                started = datetime.now(UTC).replace(tzinfo=None)

                print(f"{good:3d}: {cur[5:-3]:<15} ", flush=True, end="")

                try:
                    subprocess.check_call([exe])
                except:
                    for key, value in os.environ.items():
                        if key.startswith("MICROKEYS_"):
                            print(f"{key}={value}")
                    raise

                finished = datetime.now(UTC).replace(tzinfo=None)
                runtime = (finished - started).total_seconds()

                if os.path.isfile(expected) or os.path.isfile(expected_py):
                    if os.path.isfile(expected_py):
                        # Use a python script to generate the expected output for
                        # any test that will output a silly amount of data
                        expected_data = subprocess.check_output(f"python3 {expected_py}", shell=True).decode("utf-8").replace("\r", "")
                    else:
                        with open(expected) as f:
                            expected_data = f.read()
                    with open(temp) as f:
                        test_data = f.read()
                    if test_data != expected_data:
                        print("failed!")
                        print(f"ERROR: {cur} did not complete properly!")
                        if os.path.isfile(expected_py):
                            print(f"Compare {temp} to results of {expected_py}")
                        else:
                            print(f"Compare {temp} to {expected}")
                        exit(1)
                else:
                    print("unknown")
                    print(f"WARNING: {cur} does not have test results")
                    print(f"Check {temp}, and if it's good, rename it to {expected}")
                    exit(1)
                
                os.unlink(temp)
                good += 1
                print(f"is good, took {runtime:0.3f} seconds")

    if good == 1:
        print("Test ran without problems")
    else:
        print(f"All {good} tests are good")

if __name__ == "__main__":
    build_type = None
    tests = None
    if len(sys.argv) > 1:
        build_type = sys.argv[1]
    if len(sys.argv) > 2:
        tests = set(sys.argv[2].lower().split(","))
    if build_type is None or build_type.lower() not in {"debug", "release"}:
        print("Usage:")
        print("  run_all.py <Debug|Release> (<tests to run>)")
        exit(1)
    main(build_type, tests)
