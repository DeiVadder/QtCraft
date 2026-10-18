#!/usr/bin/env python3
"""Test selection for scripts/verify.sh --changed (decision E-405).

A test is selected when one of its inputs changed since the test last passed in this build
directory. Its inputs are:

  - the executables of the build directory named on its command line or in its REQUIRED_FILES
    (the test binary, qtcraft, qtcraft-headless): a relink changes their modification time;
  - the source files and directories named there (scripts, scenario files, runtime-read source
    folders such as src/app/qml for test_ui_palette);
  - the runtime data that every test may read (RUNTIME_DATA below: content, translations, mods,
    shaders, test data, handbook exports, CMake files and presets). A change there selects all.

A file counts as changed when its modification time or size differs. The fingerprints of the
last passing run of each test live in <build dir>/.verify-test-stamps.json; a test's entry is
only renewed after it passed (JUnit report of ctest), so a failed or interrupted run selects it
again next time. Without a stamp file every test runs.

  select  writes the current fingerprints to <build dir>/.verify-test-pending.json and, with
          --changed, prints the ctest regex of the selected tests on stdout (empty: none) and the
          reasons on stderr. Without --changed it only writes the pending fingerprints.
  record  copies the pending fingerprints of the tests the JUnit reports show as passed (and of
          the adopted tests, see below) into the stamp file. --junit may be given once per ctest
          phase (verify.sh runs the `timing` tests after the rest, decision E-550); a test that
          did not run (a deferred timing test) keeps its old stamp and is selected again.

--broad-label <l> (the TSan step of --tsan-concurrent --changed, decision E-412) widens the
selection beyond the label without running every test under TSan: tests with label <l> are
selected as with --label; every other test only when its own inputs changed since it last passed
(relinked binary, changed script) or, without a stamp, when a test source named after it
(tests/**/<name>.*) differs from the base branch (QC_VERIFY_BASE, default dev): a new or edited
test. A change of runtime data alone does not select them. Tests without a stamp and without such
a change are adopted: they do not run, and their current fingerprints become their stamps, so the
next relink selects them. That keeps the first run in a fresh build directory at the label.

Usage: scripts/verify-tests.py select --build-dir <dir> [--changed] [--regex <re>] [--label <l>]
                                      [--broad-label <l>]
       scripts/verify-tests.py record --build-dir <dir> --junit <report.xml> [--junit <report.xml>]
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STAMPS = ".verify-test-stamps.json"
PENDING = ".verify-test-pending.json"

# Paths (relative to the project) whose change selects every test: data the tests read at run
# time without a relink, and the build description (test properties, environment, presets).
RUNTIME_DATA = [
    "content", "translations", "mods", "tests/data", "tests/scenarios", "tests/scripts",
    "tests/ui", "tests/expect_exit_code.cmake", "tests/tsan.supp", "docs/handbook",
    "web/handbook", "cmake", "CMakePresets.json", "CMakeUserPresets.json", "CMakeLists.txt",
]
# Directories searched for further CMake files (CMakeLists.txt, the test lists tests/*/tests.cmake)
# and shader folders.
NESTED_DATA_ROOTS = ["src", "tests"]
# How many changed runtime data paths the report names.
MAX_NAMED_PATHS = 5


def file_state(path):
    """Modification time and size of one file, or a marker when it is missing."""
    try:
        info = os.stat(path)
    except OSError:
        return "missing"
    return f"{info.st_mtime_ns} {info.st_size}"


def walk_files(path):
    """Every file below a path (the path itself when it is a file), sorted."""
    if not os.path.isdir(path):
        return [path]

    files = []
    for directory, subdirectories, names in os.walk(path):
        subdirectories.sort()
        files.extend(os.path.join(directory, name) for name in sorted(names))
    return files


def runtime_data_paths():
    """The runtime data paths that exist, including nested CMake files and shader folders."""
    paths = [os.path.join(ROOT, path) for path in RUNTIME_DATA]
    for root in NESTED_DATA_ROOTS:
        for directory, subdirectories, names in os.walk(os.path.join(ROOT, root)):
            for name in sorted(names):
                if name == "CMakeLists.txt" or name.endswith(".cmake"):
                    paths.append(os.path.join(directory, name))
            if os.path.basename(directory) == "shaders":
                paths.append(directory)
                subdirectories.clear()
    return [path for path in paths if os.path.exists(path)]


def snapshot(paths):
    """State of every file below the given paths, keyed by the path relative to the project."""
    states = {}
    for path in paths:
        for file in walk_files(path):
            states[os.path.relpath(file, ROOT)] = file_state(file)
    return states


def digest(states):
    """One hash over a snapshot."""
    text = "\n".join(f"{path} {state}" for path, state in sorted(states.items()))
    return hashlib.sha1(text.encode()).hexdigest()


def load_tests(build_dir):
    """The tests of a build directory as ctest describes them (command and properties)."""
    result = subprocess.run(["ctest", "--test-dir", build_dir, "--show-only=json-v1"],
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.exit(f"ctest --show-only failed: {result.stderr.strip()}")

    tests = {}
    for test in json.loads(result.stdout).get("tests", []):
        properties = {entry["name"]: entry["value"] for entry in test.get("properties", [])}
        tests[test["name"]] = {"command": test.get("command", []), "properties": properties}
    return tests


def candidate_paths(test):
    """Absolute paths named on the command line (also inside -DNAME=a;b) and in REQUIRED_FILES."""
    words = list(test["command"]) + list(test["properties"].get("REQUIRED_FILES", []))
    paths = []
    for word in words:
        for part in re.split(r"[;=]", word):
            if os.path.isabs(part):
                paths.append(os.path.normpath(part))
    return paths


def test_inputs(test, build_dir):
    """The inputs of one test: executables of the build directory and project source paths."""
    build_root = os.path.join(ROOT, "build") + os.sep
    inputs = []
    for path in candidate_paths(test):
        if path.startswith(build_dir + os.sep):
            # Outputs such as screenshots also live there; only executables are inputs.
            if os.path.isfile(path) and os.access(path, os.X_OK):
                inputs.append(path)
        elif path.startswith(ROOT + os.sep) and not path.startswith(build_root):
            inputs.append(path)
    return sorted(set(inputs))


def has_label(test, label):
    return label in test["properties"].get("LABELS", [])


def matches(name, test, regex, label):
    """Whether a test passes the --regex and --label filters of the run."""
    if regex and not re.search(regex, name):
        return False
    if label and not has_label(test, label):
        return False
    return True


def branch_test_names():
    """Stems of the files below tests/ that differ from the base branch (committed or not)."""
    base = os.environ.get("QC_VERIFY_BASE", "dev")
    commands = [["git", "diff", "--name-only", f"{base}...HEAD", "--", "tests"],
                ["git", "diff", "--name-only", "HEAD", "--", "tests"],
                ["git", "ls-files", "--others", "--exclude-standard", "--", "tests"]]
    names = set()
    for command in commands:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
        for path in result.stdout.split():
            names.add(os.path.splitext(os.path.basename(path))[0])
    return names


def broad_reason(name, test, current, stamp, context):
    """Reason to run a test outside the broad label, or None; adopts unchanged tests without a stamp."""
    label, branch_names, adopted = context
    if has_label(test, label):
        return reason(current, stamp)
    if stamp is None:
        if name in branch_names:
            return "new or edited on this branch"
        adopted.append(name)
        return None
    if stamp.get("inputs") != current["inputs"]:
        return "relinked or inputs changed"
    return None


def read_json(path, default):
    try:
        with open(path, encoding="utf-8") as file:
            return json.load(file)
    except (OSError, ValueError):
        return default


def write_json(path, value):
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8") as file:
        json.dump(value, file, indent=1, sort_keys=True)
    os.replace(temporary, path)


def fingerprints(tests, build_dir, data_digest):
    """Current fingerprint of every test: the state of its inputs and the runtime data digest."""
    result = {}
    for name, test in tests.items():
        inputs = snapshot(test_inputs(test, build_dir))
        result[name] = {"inputs": digest(inputs), "data": data_digest}
    return result


def reason(current, stamp):
    """Why a test runs, or None when it passed with exactly these inputs."""
    if stamp is None:
        return "no stamp"
    reasons = []
    if stamp.get("inputs") != current["inputs"]:
        reasons.append("relinked or inputs changed")
    if stamp.get("data") != current["data"]:
        reasons.append("runtime data changed")
    return ", ".join(reasons) or None


def changed_data_paths(old, new):
    """Runtime data files that differ between the last recorded snapshot and now."""
    paths = sorted(path for path in set(old) | set(new) if old.get(path) != new.get(path))
    named = ", ".join(paths[:MAX_NAMED_PATHS])
    if len(paths) > MAX_NAMED_PATHS:
        named += f" and {len(paths) - MAX_NAMED_PATHS} more"
    return named


def report(selected, considered, stamps, data_now, broad):
    """Prints the selection and its reasons on stderr."""
    out = sys.stderr
    if not stamps.get("tests") and not broad:
        print(f"verify --changed: no stamps in this build directory, running all {len(considered)} tests", file=out)
        return

    groups = {}
    for name, why in selected:
        groups.setdefault(why, []).append(name)
    for why, names in sorted(groups.items()):
        print(f"verify --changed: {len(names)} test(s), {why}: {' '.join(names)}", file=out)
    if any("runtime data" in why for why in groups):
        named = changed_data_paths(stamps.get("data_files", {}), data_now)
        if named:
            print(f"verify --changed: runtime data changed since the last recorded run: {named}", file=out)
    skipped = len(considered) - len(selected)
    print(f"verify --changed: {len(selected)} of {len(considered)} tests selected, {skipped} skipped (unchanged since they passed)", file=out)


def select(arguments):
    build_dir = os.path.abspath(arguments.build_dir)
    tests = load_tests(build_dir)
    data_now = snapshot(runtime_data_paths())
    current = fingerprints(tests, build_dir, digest(data_now))
    pending = {"tests": current, "data_files": data_now}
    write_json(os.path.join(build_dir, PENDING), pending)
    if not arguments.changed:
        return

    stamps = read_json(os.path.join(build_dir, STAMPS), {})
    known = stamps.get("tests", {})
    considered = [name for name, test in tests.items()
                  if matches(name, test, arguments.regex, arguments.label)]
    selected = []
    adopted = []
    context = None
    if arguments.broad_label:
        context = (arguments.broad_label, branch_test_names(), adopted)
    for name in sorted(considered):
        if context:
            why = broad_reason(name, tests[name], current[name], known.get(name), context)
        else:
            why = reason(current[name], known.get(name))
        if why:
            selected.append((name, why))

    report(selected, considered, stamps, data_now, bool(arguments.broad_label))
    if adopted:
        pending["adopted"] = adopted
        write_json(os.path.join(build_dir, PENDING), pending)
        print(f"verify --changed: {len(adopted)} test(s) outside label {arguments.broad_label} without a stamp "
              "adopted as unchanged (they run after their next relink)", file=sys.stderr)
    if selected:
        print("^(" + "|".join(re.escape(name) for name, _ in selected) + ")$")


def passed_tests(junit):
    """Names of the tests the JUnit report of ctest shows as passed."""
    try:
        root = ElementTree.parse(junit).getroot()
    except (OSError, ElementTree.ParseError):
        return []

    passed = []
    for case in root.iter("testcase"):
        failed = case.find("failure") is not None or case.find("error") is not None
        if case.get("status") == "run" and not failed and case.find("skipped") is None:
            passed.append(case.get("name"))
    return passed


def record(arguments):
    build_dir = os.path.abspath(arguments.build_dir)
    pending = read_json(os.path.join(build_dir, PENDING), None)
    if pending is None:
        return

    os.remove(os.path.join(build_dir, PENDING))
    stamps_path = os.path.join(build_dir, STAMPS)
    stamps = read_json(stamps_path, {})
    known = stamps.setdefault("tests", {})
    reports = [name for junit in arguments.junit for name in passed_tests(junit)]
    passed = [name for name in reports if name in pending["tests"]]
    adopted = pending.get("adopted", [])
    for name in passed + adopted:
        known[name] = pending["tests"][name]
    if passed or adopted:
        stamps["data_files"] = pending["data_files"]
        write_json(stamps_path, stamps)
    print(f"verify: recorded {len(passed)} passed and {len(adopted)} adopted test(s) in {STAMPS}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=["select", "record"])
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--changed", action="store_true")
    parser.add_argument("--regex", default="")
    parser.add_argument("--label", default="")
    parser.add_argument("--broad-label", default="")
    parser.add_argument("--junit", action="append", default=[])
    arguments = parser.parse_args()
    if arguments.command == "select":
        select(arguments)
    else:
        record(arguments)


if __name__ == "__main__":
    main()
