#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

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

Tests labelled `realtime` (tests/CMakeLists.txt) mostly wait for timers, links with latency or
other processes: their CPU time is under a quarter of their wall time in debug, and TSan slows
them down far less (measured 2026-10-16 in a parallel TSan run: 1.2-3.5 x their debug time,
test_survival_scenario 48 -> 56 s, test_latency 28 -> 54 s, test_multiplayer_bots 17 -> 61 s).
For them the factor is REALTIME_FACTOR (about twice the typical TSan slowdown), and the COST
threshold grows by FACTOR / REALTIME_FACTOR, so it stands for the same TSan time.

A single debug time says little on a busy machine (another session building beside the run made
every test 2-2.5 x slower in one run). So every run adds its times to <build dir>/
.verify-test-times.json, which keeps the last HISTORY times per test, and the check uses the
smallest of them: one slow run under load does not warn, a test that really got slower warns
once its recent runs are all slow.

Warnings only, never a failure: debug times in a parallel run vary with the load beside it, and
the person reading the warning decides (set TIMEOUT/COST in tests/CMakeLists.txt or make the
test lighter). Prints nothing when every limit fits.

The debug step runs in two phases (the `timing` tests after the rest, decision E-550); --junit
is given once per phase and the check covers the tests of both.

Usage: scripts/verify-limits.py --build-dir <dir> --junit <report.xml> [--junit <report.xml>]
"""

import argparse
import json
import math
import os
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

FACTOR = 15
COST_SECONDS = 15
REALTIME_LABEL = "realtime"
REALTIME_FACTOR = 5
HISTORY = 5
TIMES_FILE = ".verify-test-times.json"
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


def recent_times(build_dir, times):
    """Adds this run's times to the history of the build directory; the smallest recent time per test."""
    path = os.path.join(build_dir, TIMES_FILE)
    try:
        with open(path, encoding="utf-8") as file:
            history = json.load(file)
    except (OSError, ValueError):
        history = {}

    for name, seconds in times.items():
        history[name] = (history.get(name, []) + [round(seconds, 2)])[-HISTORY:]
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as file:
        json.dump(history, file, indent=1, sort_keys=True)
    os.replace(temporary, path)
    return {name: min(history[name]) for name in times}


def round_up(value, step):
    return int(math.ceil(value / step) * step)


def warnings(times, properties):
    """One line per test whose limits do not fit its debug time."""
    lines = []
    for name, seconds in sorted(times.items(), key=lambda item: -item[1]):
        values = properties.get(name, {})
        timeout = float(values.get("TIMEOUT", DEFAULT_TIMEOUT))
        factor = REALTIME_FACTOR if REALTIME_LABEL in values.get("LABELS", []) else FACTOR
        cost_seconds = COST_SECONDS * FACTOR / factor
        problems = []
        if timeout < factor * seconds:
            problems.append(f"TIMEOUT {timeout:.0f} s < {factor} x debug time, "
                            f"suggest TIMEOUT {round_up(factor * seconds, 30)}")
        if seconds > cost_seconds and "COST" not in values:
            # The existing COST entries are about half the test's parallel TSan time in seconds.
            problems.append(f"over {cost_seconds:.0f} s without COST, "
                            f"suggest COST {round_up(seconds * factor / 4, 10)}")
        if problems:
            lines.append(f"LIMIT WARNING {name}: debug {seconds:.1f} s (fastest of the last {HISTORY} runs); "
                         + "; ".join(problems))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--junit", action="append", required=True)
    arguments = parser.parse_args()

    times = {}
    for junit in arguments.junit:
        times.update(run_times(junit))
    if not times:
        return
    smallest = recent_times(arguments.build_dir, times)
    for line in warnings(smallest, test_properties(arguments.build_dir)):
        print(line)


if __name__ == "__main__":
    main()
