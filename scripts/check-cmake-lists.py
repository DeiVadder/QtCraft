#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Checks the source lists of the CMake files under src/ (decision E-417).

Every target lists its sources in one `target_sources(<target> PRIVATE ...)` block per folder (the
executables name their few files in `qt_add_executable`). The check fails when
  - the lines of a source block are not sorted (case-insensitive, by path), files on the command's
    first line included,
  - a target_sources block names files of several folders, the same folder appears in two blocks of a
    target, or the blocks of a target outside `if()` are not sorted by folder,
  - a line holds files of different classes: more than one header, a header that is not the first
    file, or a file whose name does not start with the name of the first file (a header goes next to
    its sources: `QcName.h QcName.cpp QcNameParts.cpp`),
  - files of one name (`QcName.h`, `QcName.cpp`) stand on different lines of a block,
  - a line after the command's first line mixes files with other arguments,
  - a C++, Objective-C++ or QML file under src/ (outside shaders/) is listed in no CMake file under src/,
  - a file named in a CMake file under src/ or tests/ does not exist, relative or as
    `${PROJECT_SOURCE_DIR}/...`.
Usage: scripts/check-cmake-lists.py [--self-test]
  (exit code 0: all lists fine, 1: findings, printed one per line; --self-test checks bad samples in a
  temporary folder and fails if the check misses one)
"""

import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_SUFFIXES = (".h", ".cpp", ".mm", ".qml")
# Folders under src/ whose files are not sources of a target (shaders, compiled by qt_add_shaders).
SKIPPED_FOLDERS = {"shaders"}
# A file name in a CMake file: a relative path ending in a source suffix, without variables.
FILE_TOKEN = re.compile(r"(?<![\w${}/.-])([\w./-]+\.(?:h|cpp|mm|qml))(?![\w])")
# A path below the project folder: `${PROJECT_SOURCE_DIR}/<path>` up to a separator or a variable.
PROJECT_PATH = re.compile(r"\$\{PROJECT_SOURCE_DIR\}/([^\s;\"()$]+)")
# Commands whose arguments are a source list that must be sorted.
SOURCE_BLOCK = re.compile(r"^\s*(target_sources|qt_add_executable|qt_add_library)\s*\(")
COMMAND_NAME = re.compile(r"^\s*(\w+)\s*\(")


def cmake_files(root):
    """Every CMakeLists.txt and *.cmake file below a folder, sorted."""
    found = []
    for directory, subdirectories, names in os.walk(root):
        subdirectories.sort()
        for name in sorted(names):
            if name == "CMakeLists.txt" or name.endswith(".cmake"):
                found.append(os.path.join(directory, name))
    return found


def strip_comment(line):
    """The line without a trailing `#` comment (no quoted `#` occurs in the source lists)."""
    return line.split("#", 1)[0]


def command_calls(text):
    """(command line index, list of argument lines) for each command call of a CMake file."""
    lines = text.splitlines()
    calls = []
    index = 0
    while index < len(lines):
        line = strip_comment(lines[index])
        if "(" not in line:
            index += 1
            continue

        start = index
        depth = line.count("(") - line.count(")")
        body = [line]
        while depth > 0 and index + 1 < len(lines):
            index += 1
            next_line = strip_comment(lines[index])
            depth += next_line.count("(") - next_line.count(")")
            body.append(next_line)
        calls.append((start, body))
        index += 1
    return calls


def stem(path):
    """The file name without folder and suffix."""
    return os.path.splitext(os.path.basename(path))[0]


def block_entries(cmake_path, start, body, root):
    """(entries, findings) of a source block: entries are (line number, files) per line with files,
    the command's first line counted from the opening parenthesis."""
    relative = os.path.relpath(cmake_path, root)
    findings = []
    entries = []
    for offset, line in enumerate(body):
        text = line.split("(", 1)[1] if offset == 0 else line
        tokens = text.replace(")", " ").split()
        files = [token for token in tokens if FILE_TOKEN.fullmatch(token)]
        if not files:
            continue

        line_number = start + offset + 1
        if offset > 0 and len(files) < len(tokens):
            findings.append(f"{relative}:{line_number}: one line mixes files and other arguments: {text.strip()}")
        entries.append((line_number, files))
    return entries, findings


def check_line_classes(relative, entries):
    """Findings for lines that hold files of different classes."""
    findings = []
    for line_number, files in entries:
        headers = [name for name in files if name.endswith(".h")]
        mixed = len(headers) > 1 or (headers and headers[0] != files[0])
        if mixed or not all(stem(name).startswith(stem(files[0])) for name in files[1:]):
            findings.append(f"{relative}:{line_number}: one line holds files of different classes: {' '.join(files)}")
    return findings


def check_block(cmake_path, start, body, root):
    """Findings for one source block: order of the lines and pairs of header and source."""
    relative = os.path.relpath(cmake_path, root)
    entries, findings = block_entries(cmake_path, start, body, root)
    findings.extend(check_line_classes(relative, entries))

    keys = [files[0].lower() for _, files in entries]
    for (line_number, files), previous, current in zip(entries[1:], keys, keys[1:]):
        if current < previous:
            findings.append(f"{relative}:{line_number}: {files[0]} is out of order (sort the block alphabetically)")

    lines_of_name = {}
    for line_number, files in entries:
        for name in files:
            key = os.path.join(os.path.dirname(name), stem(name))
            lines_of_name.setdefault(key, set()).add(line_number)
    for key, line_numbers in sorted(lines_of_name.items()):
        if len(line_numbers) > 1:
            findings.append(f"{relative}:{min(line_numbers)}: {key}.* is split over several lines "
                            "(header next to source)")
    return findings, entries


def block_folder(relative, start, entries):
    """(folder, findings) of a target_sources block: the one folder of its files."""
    folders = sorted({os.path.dirname(name) for _, files in entries for name in files})
    if len(folders) > 1:
        finding = f"{relative}:{start + 1}: one target_sources block names several folders: {' '.join(folders)}"
        return folders[0], [finding]
    return (folders[0] if folders else None), []


def check_block_order(relative, blocks):
    """Findings for the target_sources blocks of a file outside if() (the platform blocks follow the
    sorted ones): one block per folder and target, the blocks sorted by folder.
    blocks: (line index, target, folder, outside if())."""
    findings = []
    seen = {}
    previous = {}
    for start, target, folder, top_level in blocks:
        if folder is None or not top_level:
            continue

        if (target, folder) in seen:
            findings.append(f"{relative}:{start + 1}: folder {folder or '.'} of {target} is split over several blocks "
                            f"(first at line {seen[(target, folder)] + 1})")
        seen.setdefault((target, folder), start)
        if target in previous and folder.lower() < previous[target].lower():
            findings.append(f"{relative}:{start + 1}: block of folder {folder or '.'} is out of folder order")
        previous[target] = folder
    return findings


def listed_files(cmake_path, body, root):
    """Absolute paths of the files a command names: relative to the folder of its CMake file under
    src/, relative to tests/ for the test lists (the topic files are included into tests/CMakeLists.txt),
    and below the project folder for `${PROJECT_SOURCE_DIR}/...`."""
    source_root = os.path.join(root, "src")
    in_sources = cmake_path.startswith(source_root + os.sep)
    folder = os.path.dirname(cmake_path) if in_sources else os.path.join(root, "tests")
    files = []
    for line in body:
        for token in FILE_TOKEN.findall(line):
            files.append(os.path.normpath(os.path.join(folder, token)))
        for token in PROJECT_PATH.findall(line):
            files.append(os.path.normpath(os.path.join(root, token)))
    return files


def source_files(root):
    """Every C++, Objective-C++ and QML file under src/ outside the skipped folders."""
    found = set()
    for directory, subdirectories, names in os.walk(os.path.join(root, "src")):
        subdirectories[:] = sorted(name for name in subdirectories if name not in SKIPPED_FOLDERS)
        for name in names:
            if name.endswith(SOURCE_SUFFIXES):
                found.add(os.path.join(directory, name))
    return found


def check_file(cmake_path, root, listed):
    """Findings for one CMake file; adds the files it names under src/ to `listed`."""
    with open(cmake_path, encoding="utf-8") as handle:
        text = handle.read()
    relative = os.path.relpath(cmake_path, root)
    in_sources = cmake_path.startswith(os.path.join(root, "src") + os.sep)
    findings = []
    blocks = []
    if_depth = 0
    for start, body in command_calls(text):
        name = COMMAND_NAME.match(body[0])
        command = name.group(1).lower() if name else ""
        if_depth += 1 if command == "if" else -1 if command == "endif" else 0

        files = listed_files(cmake_path, body, root)
        for path in files:
            if not os.path.exists(path):
                findings.append(f"{relative}:{start + 1}: listed file does not exist: {os.path.relpath(path, root)}")
        if not in_sources:
            continue

        listed.update(files)
        if SOURCE_BLOCK.match(body[0]):
            block_findings, entries = check_block(cmake_path, start, body, root)
            findings.extend(block_findings)
            if command == "target_sources":
                folder, folder_findings = block_folder(relative, start, entries)
                findings.extend(folder_findings)
                arguments = body[0].split("(", 1)[1].split()
                target = arguments[0] if arguments else ""
                blocks.append((start, target, folder, if_depth == 0))
    findings.extend(check_block_order(relative, blocks))
    return findings


def check(root):
    """Every finding of the CMake files below a project folder."""
    findings = []
    listed = set()
    for cmake_path in cmake_files(os.path.join(root, "src")) + cmake_files(os.path.join(root, "tests")):
        findings.extend(check_file(cmake_path, root, listed))

    for path in sorted(source_files(root) - listed):
        findings.append(f"{os.path.relpath(path, root)}: not listed in any CMake file under src/")
    return findings


# Bad samples of the self-test: (CMake text of src/lib/CMakeLists.txt, files to create besides the
# relative ones the text names, files of the text to leave out, expected part of a finding).
SELF_TEST_CASES = [
    ("target_sources(lib PRIVATE a/QcZ.h a/QcZ.cpp\n    a/QcA.h a/QcA.cpp\n)\n", [], [], "out of order"),
    ("target_sources(lib PRIVATE\n    a/QcA.h a/QcA.cpp PUBLIC\n)\n", [], [], "mixes files and other arguments"),
    ("target_sources(lib PRIVATE\n    a/QcRails.h a/QcRails.cpp a/QcRailsShape.cpp\n    a/QcRailsShape.h\n)\n",
     [], [], "a/QcRailsShape.* is split over several lines"),
    ("target_sources(lib PRIVATE\n    a/QcA.h a/QcAb.h a/QcA.cpp\n)\n", [], [], "files of different classes"),
    ("target_sources(lib PRIVATE\n    a/QcA.cpp a/QcA.h\n)\n", [], [], "files of different classes"),
    ("target_sources(lib PRIVATE\n    a/QcA.h a/QcA.cpp\n    b/QcB.h b/QcB.cpp\n)\n", [], [], "names several folders"),
    ("target_sources(lib PRIVATE\n    b/QcB.h b/QcB.cpp\n)\ntarget_sources(lib PRIVATE\n    a/QcA.h a/QcA.cpp\n)\n",
     [], [], "out of folder order"),
    ("target_sources(lib PRIVATE\n    a/QcA.h\n)\ntarget_sources(lib PRIVATE\n    a/QcB.h\n)\n", [], [],
     "split over several blocks"),
    ("target_sources(lib PRIVATE a/QcA.h)\nset(x \"${PROJECT_SOURCE_DIR}/src/lib/a/QcMissing.h\")\n", [], [],
     "listed file does not exist: src/lib/a/QcMissing.h"),
    ("target_sources(lib PRIVATE a/QcA.h a/QcA.cpp)\n", [], ["a/QcA.cpp"], "listed file does not exist"),
    ("target_sources(lib PRIVATE a/QcA.h)\n", ["a/QcA.cpp"], [], "a/QcA.cpp: not listed"),
    ("target_sources(lib PRIVATE a/QcA.h)\nqt_add_qml_module(lib QML_FILES\n    qml/Main.qml\n)\n",
     ["qml/Other.qml"], [], "qml/Other.qml: not listed"),
]
SELF_TEST_GOOD = (
    "qt_add_library(lib STATIC)\n"
    "target_sources(lib PRIVATE\n    a/QcA.h a/QcA.cpp a/QcAParts.cpp\n    a/QcB.h\n)\n"
    "target_sources(lib PRIVATE b/QcC.h b/QcC.cpp)\n"
    "if(APPLE)\n    target_sources(lib PRIVATE a/QcA_mac.mm)\nendif()\n"
    "qt_add_qml_module(lib QML_FILES\n    qml/Main.qml\n)\n"
    "set(x \"${PROJECT_SOURCE_DIR}/src/lib/qml\")\n"
)


def write_sample(root, text, extra, missing):
    """Writes src/lib/CMakeLists.txt with `text` and creates the files it names (except `missing`)
    and the `extra` files."""
    folder = os.path.join(root, "src", "lib")
    os.makedirs(folder)
    with open(os.path.join(folder, "CMakeLists.txt"), "w", encoding="utf-8") as handle:
        handle.write(text)
    for name in set(FILE_TOKEN.findall(text) + extra) - set(missing):
        path = os.path.join(folder, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w", encoding="utf-8").close()


def self_test():
    """Runs the check on the bad samples and the good one; 0 when it finds exactly what it should."""
    failures = []
    for index, (text, extra, missing, expected) in enumerate(SELF_TEST_CASES):
        with tempfile.TemporaryDirectory() as root:
            write_sample(root, text, extra, missing)
            findings = check(root)
        if not any(expected in finding for finding in findings):
            failures.append(f"sample {index}: expected a finding with '{expected}', got {findings}")

    with tempfile.TemporaryDirectory() as root:
        write_sample(root, SELF_TEST_GOOD, [], [])
        findings = check(root)
    if findings:
        failures.append(f"good sample: expected no findings, got {findings}")

    for failure in failures:
        print(failure)
    if failures:
        print(f"check-cmake-lists self-test: {len(failures)} failure(s)", file=sys.stderr)
        return 1
    print(f"check-cmake-lists self-test: {len(SELF_TEST_CASES)} bad samples found, good sample clean")
    return 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()

    findings = check(ROOT)
    for finding in findings:
        print(finding)
    if findings:
        print(f"check-cmake-lists: {len(findings)} finding(s)", file=sys.stderr)
        return 1
    print("check-cmake-lists: source lists sorted and complete")
    return 0


if __name__ == "__main__":
    sys.exit(main())
