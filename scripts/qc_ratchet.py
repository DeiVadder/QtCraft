# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ratchet of the counting checks (row 79.48b, E-942): the number of findings of a rule may only go down.

scripts/check-baseline.json holds, per script, the highest allowed count of each ratcheted rule. A count
above it fails the check; a count below it is printed with the hint to lower the baseline, so a fixed
finding cannot come back unnoticed. A missing or unreadable baseline ends the calling script with exit
code 2. A rule of the baseline that the script does not count is an error too (a renamed rule would
otherwise never be checked).
Usage: scripts/qc_ratchet.py --self-test
"""

import json
import os
import sys
import tempfile

BASELINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "check-baseline.json")


def load(script, path=BASELINE):
    """The baseline of `script` ({rule: allowed count}); exits with 2 when the file is missing or broken."""
    try:
        with open(path, encoding="utf-8") as stream:
            return json.load(stream).get(script, {})
    except (OSError, ValueError) as error:
        print(f"ratchet: cannot read the baseline {path}: {error}", file=sys.stderr)
        sys.exit(2)


def judge(baseline, counts):
    """Compares `counts` ({rule: findings}) with `baseline`; returns the lines to print and the number of
    rules above their baseline or unknown to the counts."""
    lines = []
    failed = 0
    for rule, allowed in sorted(baseline.items()):
        if rule not in counts:
            failed += 1
            lines.append(f"ratchet: the baseline names {rule}, which this check does not count; fix "
                         "scripts/check-baseline.json")
            continue
        number = counts[rule]
        if number > allowed:
            failed += 1
            lines.append(f"ratchet: {rule} rose to {number} (baseline {allowed} in scripts/check-baseline.json); "
                         "fix the new findings")
        elif number < allowed:
            lines.append(f"ratchet: {rule} fell to {number} (baseline {allowed}); lower it in "
                         "scripts/check-baseline.json")
    return lines, failed


def check(script, counts, path=BASELINE):
    """Compares `counts` of `script` with the baseline at `path`, prints a line per rule that rose, fell or
    is unknown, and returns the number of rules that fail."""
    lines, failed = judge(load(script, path), counts)
    for line in lines:
        print(line)
    return failed


def self_test():
    failures = []
    baseline = {"clone": 4, "repeat": 2, "gone": 1}
    lines, failed = judge(baseline, {"clone": 5, "repeat": 1, "gone": 1})
    if failed != 1 or not any("clone rose to 5" in line for line in lines) \
            or not any("repeat fell to 1" in line for line in lines):
        failures.append(f"a rise fails, a fall only prints: {lines}")
    lines, failed = judge(baseline, {"clone": 4, "repeat": 2, "gone": 1})
    if failed or lines:
        failures.append(f"equal counts print nothing: {lines}")
    lines, failed = judge(baseline, {"clone": 4, "repeat": 2})
    if failed != 1 or "gone" not in "".join(lines):
        failures.append(f"a rule unknown to the counts fails: {lines}")
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "baseline.json")
        with open(path, "w", encoding="utf-8") as stream:
            json.dump({"a.py": baseline}, stream)
        if load("a.py", path) != baseline or load("b.py", path) != {}:
            failures.append("the baseline of a script is read by its name")
        try:
            load("a.py", os.path.join(folder, "missing.json"))
            failures.append("a missing baseline ends the run")
        except SystemExit as stop:
            if stop.code != 2:
                failures.append(f"a missing baseline exits with 2, not {stop.code}")
    for failure in failures:
        print(f"qc_ratchet self-test: {failure}")
    if not failures:
        print("qc_ratchet self-test: all samples judged correctly")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(self_test() if "--self-test" in sys.argv[1:] else 2)
