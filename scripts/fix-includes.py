#!/usr/bin/env python3
"""Keeps the project includes and source paths in step with where the files are (decision E-418).

Every project include names its header by its path below an include root: `src/` for the code
(`#include "server/blocks/QcRails.h"`) and `tests/` for the shared test fixtures
(`#include "QcServerTestFixture.h"`, `#include "world/QcVillageTestUtil.h"`). File names are
unique in the project (`Qc` prefix, one class per file), so a header is found by its name alone.
After moving files with `git mv`, running this script

  - rewrites every `#include "..."` of a project header in src/ and tests/ (.h, .cpp, .mm) to the
    header's path below its root; generated includes (`*.moc`, `moc_*.cpp`, `ui_*.h`) and headers
    outside the project stay as they are;
  - rewrites the paths of moved files in the CMake files under src/ and tests/ (a path that no
    longer exists, but whose file name names exactly one file of the project): relative to the
    CMake file under src/, as `${PROJECT_SOURCE_DIR}/...` where the old path used that form;
  - rewrites the source locations (`<location filename="../src/...">`) of the translation files.

A moved file still needs its line in the right `target_sources` block of its library;
scripts/check-cmake-lists.py reports what is out of place.

Usage: scripts/fix-includes.py [--check] [--deps [<prefix>]]
  --check   change nothing; exit 1 and list the files that would change (verify.sh runs this)
  --deps    print which folders of src/ include headers of which other folders (with counts);
            with a prefix (e.g. server/worldgen) only the folders below it
"""

import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCLUDE_ROOTS = ("src", "tests")
CODE_SUFFIXES = (".h", ".cpp", ".mm")
SOURCE_SUFFIXES = (".h", ".cpp", ".mm", ".qml")
INCLUDE = re.compile(r'^(\s*#\s*include\s+)"([^"]+)"', re.MULTILINE)
# A file name in a CMake file: a relative path ending in a source suffix, without variables.
CMAKE_FILE = re.compile(r"(?<![\w${}/.-])((?:\$\{PROJECT_SOURCE_DIR\}/)?[\w./-]+\.(?:h|cpp|mm|qml))(?![\w])")
TS_LOCATION = re.compile(r'(<location filename=")([^"]+)(")')
TRANSLATIONS = "translations"


def walk(root, suffixes):
    """Project-relative paths of the files below a folder with one of the suffixes, sorted."""
    found = []
    for directory, subdirectories, names in os.walk(os.path.join(ROOT, root)):
        subdirectories.sort()
        for name in sorted(names):
            if name.endswith(suffixes):
                found.append(os.path.relpath(os.path.join(directory, name), ROOT))
    return found


def index_files():
    """File name -> project-relative path of every source file of src/ and tests/.

    A name that several files share maps to None (their paths are never rewritten; parts of one
    class such as src/app/main.cpp and src/headless/main.cpp). Exits with a message when two
    headers share a name: includes by name would be ambiguous."""
    by_name = {}
    clashes = []
    for root in INCLUDE_ROOTS:
        for path in walk(root, SOURCE_SUFFIXES):
            name = os.path.basename(path)
            if name in by_name and name.endswith(".h"):
                clashes.append(f"{by_name[name]} and {path}")
            by_name[name] = None if name in by_name else path
    if clashes:
        sys.exit("fix-includes: file names must be unique:\n  " + "\n  ".join(clashes))
    return by_name


def include_path(path):
    """(root, path below it) of a project file."""
    root, _, rest = path.partition("/")
    return root, rest


def rewrite_includes(path, text, by_name, problems):
    """The text of a C++ file with every project include written as its path below its root."""
    own_root = include_path(path)[0]

    def replace(match):
        name = os.path.basename(match.group(2))
        target = by_name.get(name)
        if target is None or not name.endswith(".h"):
            return match.group(0)

        root, below = include_path(target)
        if root == "tests" and own_root != "tests":
            problems.append(f"{path}: includes the test header {target}")
            return match.group(0)
        return f'{match.group(1)}"{below}"'

    return sort_include_runs(path, INCLUDE.sub(replace, text))


def sort_include_runs(path, text):
    """The text with each run of adjacent project includes sorted by path.

    A run is a sequence of `#include "..."` lines without a blank line between them; a source file's
    own header stays first, generated includes (`*.moc`) stay where they are and end a run."""
    lines = text.split("\n")
    own = os.path.splitext(os.path.basename(path))[0] + ".h"
    index = 0
    while index < len(lines):
        end = index
        while end < len(lines) and INCLUDE.match(lines[end]) and not lines[end].rstrip().endswith('.moc"'):
            end += 1
        if end - index > 1:
            run = lines[index:end]
            head = [run[0]] if os.path.basename(INCLUDE.match(run[0]).group(2)) == own else []
            lines[index:end] = head + sorted(run[len(head):], key=lambda line: INCLUDE.match(line).group(2).lower())
        index = max(end, index + 1)
    return "\n".join(lines)


def rewrite_cmake(path, text, by_name):
    """The text of a CMake file with paths of moved files pointing to where they are now.

    Relative paths count from the folder of the CMakeLists.txt that holds them or includes them
    (a `*.cmake` file counts from the nearest folder above it with a CMakeLists.txt); comments
    stay as they are."""
    folder = os.path.dirname(os.path.join(ROOT, path))
    while not path.endswith("CMakeLists.txt") and not os.path.exists(os.path.join(folder, "CMakeLists.txt")):
        folder = os.path.dirname(folder)

    def replace(match):
        token = match.group(1)
        prefixed = token.startswith("${PROJECT_SOURCE_DIR}/")
        absolute = os.path.join(ROOT, token[len("${PROJECT_SOURCE_DIR}/"):]) if prefixed \
            else os.path.join(folder, token)
        target = by_name.get(os.path.basename(token))
        if os.path.exists(absolute) or target is None:
            return token
        if prefixed:
            return "${PROJECT_SOURCE_DIR}/" + target
        return os.path.relpath(os.path.join(ROOT, target), folder)

    lines = []
    for line in text.split("\n"):
        code, hash_sign, comment = line.partition("#")
        lines.append(CMAKE_FILE.sub(replace, code) + hash_sign + comment)
    return "\n".join(lines)


def rewrite_translation(text, by_name):
    """The text of a translation file with source locations pointing to where the files are now."""
    folder = os.path.join(ROOT, TRANSLATIONS)

    def replace(match):
        location = match.group(2)
        target = by_name.get(os.path.basename(location))
        if os.path.exists(os.path.join(folder, location)) or target is None:
            return match.group(0)
        return match.group(1) + os.path.relpath(os.path.join(ROOT, target), folder) + match.group(3)

    return TS_LOCATION.sub(replace, text)


def planned_changes(by_name):
    """(path, new text) of every file whose text changes, and the problems found."""
    changes = []
    problems = []
    candidates = [(p, "code") for root in INCLUDE_ROOTS for p in walk(root, CODE_SUFFIXES)]
    candidates += [(p, "cmake") for root in INCLUDE_ROOTS for p in walk(root, (".txt", ".cmake"))
                   if p.endswith(("CMakeLists.txt", ".cmake"))]
    candidates += [(p, "ts") for p in walk(TRANSLATIONS, (".ts",))]
    for path, kind in candidates:
        with open(os.path.join(ROOT, path), encoding="utf-8") as file:
            text = file.read()
        if kind == "code":
            new = rewrite_includes(path, text, by_name, problems)
        elif kind == "cmake":
            new = rewrite_cmake(path, text, by_name)
        else:
            new = rewrite_translation(text, by_name)
        if new != text:
            changes.append((path, new))
    return changes, problems


def folder_of(path):
    """The folder of a project file below src/ (e.g. server/worldgen/villages)."""
    return os.path.dirname(include_path(path)[1]) or "."


def print_dependencies(by_name, prefix):
    """Which folders of src/ include headers of which other folders, with the number of includes."""
    edges = defaultdict(lambda: defaultdict(int))
    for path in walk("src", CODE_SUFFIXES):
        source = folder_of(path)
        if prefix and not (source == prefix or source.startswith(prefix + "/")):
            continue
        with open(os.path.join(ROOT, path), encoding="utf-8") as file:
            text = file.read()
        for match in INCLUDE.finditer(text):
            target = by_name.get(os.path.basename(match.group(2)))
            if target and target.startswith("src/") and folder_of(target) != source:
                edges[source][folder_of(target)] += 1
    for source in sorted(edges):
        targets = ", ".join(f"{t} {n}" for t, n in sorted(edges[source].items()))
        print(f"{source} -> {targets}")


def main(arguments):
    by_name = index_files()
    if "--deps" in arguments:
        position = arguments.index("--deps")
        prefix = arguments[position + 1] if position + 1 < len(arguments) else ""
        print_dependencies(by_name, prefix.rstrip("/"))
        return 0

    changes, problems = planned_changes(by_name)
    for problem in problems:
        print(f"fix-includes: {problem}")
    if "--check" in arguments:
        for path, _ in changes:
            print(f"fix-includes: {path}: includes or paths not written as decision E-418 asks "
                  "(run scripts/fix-includes.py)")
        return 1 if changes or problems else 0

    for path, new in changes:
        with open(os.path.join(ROOT, path), "w", encoding="utf-8") as file:
            file.write(new)
    print(f"fix-includes: {len(changes)} files rewritten")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
