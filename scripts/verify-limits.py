#!/usr/bin/env python3
"""Limit check after the debug test step of scripts/verify.sh (decision E-412).

Compares the debug time of every test in the JUnit report of ctest with its TIMEOUT and COST
(ctest --show-only=json-v1) and warns, so that a limit that is too tight shows up during the
work instead of first in the TSan step of the full verify:

  - TIMEOUT below FACTOR x the debug time. Under TSan in a parallel run the tests took about
    7-10 x their debug time (E-410: test_server_light 13 -> 127 s, test_village_swamp 5 -> 44 s),
    and every limit is to be at least twice the parallel TSan time (E-380); 15 x the debug time
    is the lower end of that, measured on a machine that may itself be busy.
  - more than COST_SECONDS in debug without a COST property: ctest then starts the test late in a
    fresh build directory, and it becomes the tail of the TSan step.

Warnings only, never a failure: debug times in a parallel run vary with the load beside it, and
the person reading the warning decides (set TIMEOUT/COST in tests/CMakeLists.txt or make the
test lighter). Prints nothing when every limit fits.

Usage: scripts/verify-limits.py --build-dir <dir> --junit <report.xml>
"""

import argparse
import json
import math
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

FACTOR = 15
COST_SECONDS = 15
# ctest's limit for a test without TIMEOUT (CTEST_TEST_TIMEOUT / DART_TESTING_TIMEOUT default).
DEFAULT_TIMEOUT = 1500


def test_properties(build_dir):
    """TIMEOUT and COST of every test of a build directory, as ctest describes them."""
    result = subprocess.run(["ctest", "--test-dir", build_dir, "--show-only=json-v1"],
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.exit(f"ctest --show-only failed: {result.stderr.strip()}")

    properties = {}
    for test in json.loads(result.stdout).get("tests", []):
        values = {entry["name"]: entry["value"] for entry in test.get("properties", [])}
        properties[test["name"]] = values
    return properties


def run_times(junit):
    """Seconds of every test that ran, from the JUnit report of ctest."""
    try:
        root = ElementTree.parse(junit).getroot()
    except (OSError, ElementTree.ParseError):
        return {}

    times = {}
    for case in root.iter("testcase"):
        if case.get("status") == "run" and case.get("time"):
            times[case.get("name")] = float(case.get("time"))
    return times


def round_up(value, step):
    return int(math.ceil(value / step) * step)


def warnings(times, properties):
    """One line per test whose limits do not fit its debug time."""
    lines = []
    for name, seconds in sorted(times.items(), key=lambda item: -item[1]):
        values = properties.get(name, {})
        timeout = float(values.get("TIMEOUT", DEFAULT_TIMEOUT))
        problems = []
        if timeout < FACTOR * seconds:
            problems.append(f"TIMEOUT {timeout:.0f} s < {FACTOR} x debug time, "
                            f"suggest TIMEOUT {round_up(FACTOR * seconds, 30)}")
        if seconds > COST_SECONDS and "COST" not in values:
            # The existing COST entries are about half the test's parallel TSan time in seconds.
            problems.append(f"over {COST_SECONDS} s without COST, suggest COST {round_up(seconds * 4, 10)}")
        if problems:
            lines.append(f"LIMIT WARNING {name}: debug {seconds:.1f} s; " + "; ".join(problems))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--junit", required=True)
    arguments = parser.parse_args()

    times = run_times(arguments.junit)
    if not times:
        return
    for line in warnings(times, test_properties(arguments.build_dir)):
        print(line)


if __name__ == "__main__":
    main()
