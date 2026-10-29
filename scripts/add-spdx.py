#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Adds or checks the SPDX header (licence and copyright) of the project's own source files.

Every C++, Objective-C++, QML, JavaScript, shader, CMake, Python, shell, PowerShell, workflow, CSS and
HTML file of the project starts with two comment lines, right after a byte order mark, a shebang or
an HTML doctype:
    // SPDX-FileCopyrightText: 2026 the QtCraft authors
    // SPDX-License-Identifier: GPL-3.0-or-later
exactly so, in the comment style of the file, followed by a blank line. Only files tracked by git
count. Not covered: third-party code (3rdparty/), generated data (web/handbook/data/), test data
(tests/data/), content and mods as JSON (no comments; REUSE.toml covers them), documentation and
licence texts. The lines are comments, so they do not count towards the size limits
(scripts/qc_code_lines.py).
Usage: scripts/add-spdx.py [--check] [--changed] [--base <branch>] [--self-test] [file ...]
  without --check: adds the header to every selected file without any SPDX line and names them; a
    file with a wrong or misplaced header or another licence is named and left alone (exit code 1);
  --check: changes nothing, names every selected file whose header is missing, wrong or misplaced or
    names another licence (exit code 1 if any);
  --changed: selects only tracked files added or changed against the base branch (--base <branch>,
    anywhere in the arguments, default dev), committed or not; a base that does not exist is an error;
  files: selects these tracked files (paths relative to the project or absolute); default: all;
  --self-test: checks insertion and check on samples and fails on a wrong result.
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COPYRIGHT = "SPDX-FileCopyrightText: 2026 the QtCraft authors"
LICENSE_ID = "GPL-3.0-or-later"
LICENSE = f"SPDX-License-Identifier: {LICENSE_ID}"
BOM = "\ufeff"
# Comment style per file extension: (line start, line end).
STYLES = {
    **dict.fromkeys(("cpp", "h", "mm", "qml", "js", "frag", "vert", "glsl", "comp"), ("// ", "")),
    **dict.fromkeys(("py", "sh", "ps1", "cmake", "yml", "yaml", "supp"), ("# ", "")),
    "css": ("/* ", " */"),
    "html": ("<!-- ", " -->"),
}
# Files without a matching extension that are still sources, by name (anywhere in the tree).
NAMED_STYLES = {"CMakeLists.txt": ("# ", ""), ".clang-tidy": ("# ", ""), "pre-push": ("# ", "")}
TOP_FOLDERS = ("src/", "tests/", "scripts/", "tools/", "cmake/", "web/", ".github/workflows/")
EXCLUDED_FOLDERS = ("3rdparty/", "tests/data/", "web/handbook/data/")
# Lines at the top of a file searched for a misplaced or foreign SPDX header.
SEARCH_LINES = 12


def style_of(path):
    """The comment style of a project-relative path, or None when the file is not covered."""
    name = os.path.basename(path)
    if path.startswith(EXCLUDED_FOLDERS):
        return None
    if name in NAMED_STYLES:
        return NAMED_STYLES[name]
    if not path.startswith(TOP_FOLDERS):
        return None
    return STYLES.get(name.rsplit(".", 1)[-1]) if "." in name else None


def split(text):
    """(byte order mark, newline, lines, index of the first line the header goes on)."""
    newline = "\r\n" if "\r\n" in text else "\n"
    bom = BOM if text.startswith(BOM) else ""
    lines = text[len(bom):].split(newline)
    first = 1 if lines and (lines[0].startswith("#!") or lines[0].lower().startswith("<!doctype")) else 0
    return bom, newline, lines, first


def header_lines(style):
    start, end = style
    return [f"{start}{COPYRIGHT}{end}", f"{start}{LICENSE}{end}"]


def status(text, style):
    """"ok", "missing" (no SPDX line at the top), "foreign" (another licence) or "wrong" (an SPDX
    header that is not exactly the expected two lines at the expected place)."""
    _, _, lines, first = split(text)
    shebang_below = any(line.startswith("#!") for line in lines[1:SEARCH_LINES])
    if lines[first:first + 2] == header_lines(style) and not shebang_below:
        return "ok"
    top = lines[:SEARCH_LINES]
    licences = [line.split("SPDX-License-Identifier:", 1)[1] for line in top if "SPDX-License-Identifier:" in line]
    if any(LICENSE_ID not in licence for licence in licences):
        return "foreign"
    return "wrong" if shebang_below or any("SPDX-" in line for line in top) else "missing"


def with_header(text, style):
    """The text with the header inserted after a BOM, shebang or doctype; unchanged unless the
    header is missing (a wrong or foreign one is never doubled)."""
    if status(text, style) != "missing":
        return text
    bom, newline, lines, first = split(text)
    header = header_lines(style)
    rest = lines[first:]
    if rest and rest[0].strip():
        header.append("")
    return bom + newline.join(lines[:first] + header + rest)


def git_lines(*args):
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=False)
    return result.stdout.splitlines() if result.returncode == 0 else None


def tracked_files():
    return set(git_lines("ls-files") or [])


def changed_files(base):
    """Tracked files added or changed against the merge base with `base`, committed or not; None
    when `base` does not exist."""
    if git_lines("rev-parse", "--verify", "--quiet", f"{base}^{{commit}}") is None:
        return None
    names = set(git_lines("diff", "--name-only", "--diff-filter=AMR", f"{base}...HEAD") or [])
    names |= set(git_lines("diff", "--name-only", "--diff-filter=AMR", "HEAD") or [])
    return names


def selected(paths):
    """(relative path, style) of the covered, tracked, existing files among `paths`, sorted."""
    tracked = tracked_files()
    result = []
    for path in sorted(paths):
        relative = os.path.relpath(os.path.abspath(os.path.join(ROOT, path)), ROOT)
        style = style_of(relative)
        if style and relative in tracked and os.path.isfile(os.path.join(ROOT, relative)):
            result.append((relative, style))
    return result


MESSAGES = {
    "missing": "no SPDX header",
    "wrong": "SPDX header not exactly the two expected lines at the top (fix by hand)",
    "foreign": "SPDX-License-Identifier other than " + LICENSE_ID + " (left alone; third-party code belongs in 3rdparty/)",
}


def run(files, check):
    findings = 0
    for path, style in files:
        full = os.path.join(ROOT, path)
        with open(full, encoding="utf-8", newline="") as f:
            text = f.read()
        state = status(text, style)
        if state == "ok":
            continue
        if state == "missing" and not check:
            with open(full, "w", encoding="utf-8", newline="") as f:
                f.write(with_header(text, style))
            print(f"{path}: SPDX header added")
            continue
        findings += 1
        print(f"{path}: {MESSAGES[state]}")
    if findings:
        print(f"{findings} file(s) with SPDX header findings; scripts/add-spdx.py adds missing headers", file=sys.stderr)
    return 1 if findings else 0


C, L = COPYRIGHT, LICENSE
SELF_TEST_CASES = [
    ("src/a/QcX.h", "#pragma once\n", f"// {C}\n// {L}\n\n#pragma once\n"),
    ("scripts/x.sh", "#!/bin/sh\n# Does x.\n", f"#!/bin/sh\n# {C}\n# {L}\n\n# Does x.\n"),
    ("scripts/x.ps1", BOM + "param()\r\n", f"{BOM}# {C}\r\n# {L}\r\n\r\nparam()\r\n"),
    ("web/a.html", "<!doctype html>\n<html>\n", f"<!doctype html>\n<!-- {C} -->\n<!-- {L} -->\n\n<html>\n"),
    ("web/a.css", "\nbody {}\n", f"/* {C} */\n/* {L} */\n\nbody {{}}\n"),
    ("CMakeLists.txt", "project(X)\n", f"# {C}\n# {L}\n\nproject(X)\n"),
    ("content/CMakeLists.txt", "x()\n", f"# {C}\n# {L}\n\nx()\n"),
    (".clang-tidy", "Checks: x\n", f"# {C}\n# {L}\n\nChecks: x\n"),
]
# Files the check must reject and the insertion must leave alone: (path, text, expected status).
SELF_TEST_REJECTED = [
    ("src/a/QcX.h", f"// {L}\n\n#pragma once\n", "wrong"),
    ("src/a/QcX.h", f"// SPDX-FileCopyrightText: 2026 Someone Else\n// {L}\n", "wrong"),
    ("src/a/QcX.h", f"// A class.\n// {C}\n// {L}\n", "wrong"),
    ("src/a/QcX.h", f"# {C}\n# {L}\n", "wrong"),
    ("src/a/QcX.h", f"// {C}\n// SPDX-License-Identifier: MIT\n", "foreign"),
    ("scripts/x.sh", f"# {C}\n# {L}\n#!/bin/sh\n", "wrong"),
]
SELF_TEST_UNCOVERED = ["3rdparty/SDL/x.c", "3rdparty/SDL/CMakeLists.txt", "tests/data/x.cpp", "content/x.json",
                       "docs/x.md", "src/x.json", "web/handbook/data/theme.css", "LICENSE"]


def self_test():
    failures = []
    for path, before, expected in SELF_TEST_CASES:
        style = style_of(path)
        result = with_header(before, style) if style else None
        if result != expected:
            failures.append(f"{path}: got {result!r}")
        elif status(result, style) != "ok" or with_header(result, style) != result:
            failures.append(f"{path}: header not recognised after insertion")
    for path, text, expected in SELF_TEST_REJECTED:
        style = style_of(path)
        if status(text, style) != expected:
            failures.append(f"{path} {text!r}: status {status(text, style)}, expected {expected}")
        if with_header(text, style) != text:
            failures.append(f"{path} {text!r}: a second header was added")
    failures += [f"{path}: covered" for path in SELF_TEST_UNCOVERED if style_of(path)]
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        print(f"add-spdx self-test: {len(failures)} failure(s)", file=sys.stderr)
        return 1
    print(f"add-spdx self-test: {len(SELF_TEST_CASES)} insertions right, {len(SELF_TEST_REJECTED)} bad headers "
          f"rejected, {len(SELF_TEST_UNCOVERED)} left out")
    return 0


def main(args):
    if args == ["--self-test"]:
        return self_test()
    base = "dev"
    if "--base" in args:
        index = args.index("--base")
        if index + 1 >= len(args):
            print("--base needs a branch", file=sys.stderr)
            return 2
        base = args[index + 1]
        args = args[:index] + args[index + 2:]
    check = "--check" in args
    changed = "--changed" in args
    args = [arg for arg in args if arg not in ("--check", "--changed")]
    if any(arg.startswith("-") for arg in args):
        print(__doc__, file=sys.stderr)
        return 2
    paths = None
    if changed:
        paths = changed_files(base)
        if paths is None:
            print(f"add-spdx: base '{base}' does not exist (--changed compares against it)", file=sys.stderr)
            return 2
    if args:
        given = {os.path.relpath(os.path.abspath(os.path.join(ROOT, arg)), ROOT) for arg in args}
        paths = given if paths is None else paths & given
    return run(selected(tracked_files() if paths is None else paths), check)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
