#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Checks the file selection of the static analysis in the full verify (scripts/verify-full-select.py,
decision E-929) in a throwaway git repository with a fake stamp commit: a changed source selects
itself, a changed header itself and its direct includers (not src/server/QcFar.cpp, which reaches it
through another header); no stamp, an unknown commit, a changed analysis setup (.clang-tidy, cmake/,
the selection scripts), a changed compile definition and a selection of 40 % or more select
everything; a deleted file drops out, a renamed one counts under its new name, and a stamp commit
that is not an ancestor of HEAD is compared tree to tree.
Usage: test_verify_full_select.py <path of verify-full-select.py>
"""

import importlib.util
import os
import subprocess
import sys
import tempfile


def load_module(path):
    spec = importlib.util.spec_from_file_location("verify_full_select", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def git(root, *arguments):
    command = ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", *arguments]
    return subprocess.run(command, cwd=root, check=True, capture_output=True, text=True).stdout.strip()


def write(root, path, text):
    full = os.path.join(root, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", encoding="utf-8") as file:
        file.write(text)


FILES = {
    "src/common/QcBase.h": "struct QcBase {};\n",
    "src/common/QcBase.cpp": '#include "common/QcBase.h"\n',
    "src/common/QcMiddle.h": '#include "common/QcBase.h"\n',
    "src/server/QcUser.cpp": '#include "common/QcBase.h"\n',
    "src/server/QcFar.cpp": '#include "common/QcMiddle.h"\n',
    "src/server/QcOther.cpp": "int other;\n",
    "tests/world/test_base.cpp": '#include "common/QcBase.h"\n',
    "src/app/qml/Main.qml": "Item {}\n",
    ".clang-tidy": "Checks: '-*'\n",
    "src/CMakeLists.txt": "target_sources(qc PRIVATE QcBase.cpp)\ntarget_compile_definitions(qc PRIVATE A=1)\n",
}
# Ten more sources, so that small selections stay below the share that analyses everything (40 %).
FILES.update({f"src/worldgen/QcFiller{index}.cpp": f"int filler{index};\n" for index in range(10)})
TOTAL = 15


def check(label, actual, expected):
    if actual != expected:
        print(f"FAIL {label}: got {actual!r}, expected {expected!r}")
        return False
    print(f"ok   {label}")
    return True


def commit_all(root, message):
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", message)
    return git(root, "rev-parse", "HEAD")


def run_cases(module, root):
    stamp = commit_all(root, "green")
    line, files = module.analysis(root, stamp)
    results = [check("nothing changed", (line.split(",")[0], files), ("changed: 0 files (0 changed", [])),
               check("no stamp", module.analysis(root, ""), ("all: no green full verify recorded", [])),
               check("unknown commit", module.analysis(root, "0" * 40)[0].startswith("all: commit"), True)]

    write(root, "src/server/QcOther.cpp", "int other = 1;\n")
    commit_all(root, "source")
    results.append(check("changed source alone", module.analysis(root, stamp)[1], ["src/server/QcOther.cpp"]))

    write(root, "src/common/QcBase.h", "struct QcBase { int x; };\n")
    write(root, "src/app/qml/Main.qml", "Item { id: root }\n")
    line, files = module.analysis(root, stamp)
    results.append(check("changed header (uncommitted): itself and its direct includers", files,
                         ["src/app/qml/Main.qml", "src/common/QcBase.cpp", "src/common/QcBase.h", "src/common/QcMiddle.h",
                          "src/server/QcOther.cpp", "src/server/QcUser.cpp", "tests/world/test_base.cpp"]))
    results.append(check("summary counts", line.split(", since")[0],
                         "changed: 7 files (3 changed, 4 including a changed header), "
                         f"4 of {TOTAL} .cpp files for clang-tidy and clazy"))
    commit_all(root, "header")
    return all(results) and run_more_cases(module, root, stamp)


def setup_cases(module, root, stamp):
    """Changes of the analysis setup or of compile settings select everything; other CMake changes do not."""
    results = []
    for path, text in (("tests/.clang-tidy", "Checks: '-*,bugprone-*'\n"), ("cmake/QcFlags.cmake", "set(X 1)\n"),
                       ("scripts/verify-full-select.sh", "# changed\n")):
        write(root, path, text)
        results.append(check(f"new {path}: all", module.analysis(root, stamp)[0],
                             f"all: analysis setup changed since {stamp[:10]} ({path})"))
        os.remove(os.path.join(root, path))

    write(root, "src/CMakeLists.txt", "target_sources(qc PRIVATE QcBase.cpp QcNew.cpp)\n"
                                      "target_compile_definitions(qc PRIVATE A=1)\n")
    results.append(check("new source in a CMake list: no setup change",
                         module.analysis(root, stamp)[0].startswith("changed:"), True))
    write(root, "src/CMakeLists.txt", "target_sources(qc PRIVATE QcBase.cpp)\ntarget_compile_definitions(qc PRIVATE A=2)\n")
    results.append(check("compile definition changed: all", module.analysis(root, stamp)[0],
                         f"all: compile settings in CMake files changed since {stamp[:10]} "
                         "(target_compile_definitions(qc PRIVATE A=1))"))
    git(root, "checkout", "-q", "--", "src/CMakeLists.txt")
    return all(results)


def run_more_cases(module, root, stamp):
    results = [setup_cases(module, root, stamp)]

    # Deleted and renamed files: the deleted one drops out, the renamed one counts under its new name.
    git(root, "rm", "-q", "src/worldgen/QcFiller0.cpp")
    git(root, "mv", "src/worldgen/QcFiller1.cpp", "src/worldgen/QcRenamed.cpp")
    files = module.analysis(root, stamp)[1]
    results.append(check("deleted file drops out", "src/worldgen/QcFiller0.cpp" in files, False))
    results.append(check("renamed file under its new name", "src/worldgen/QcRenamed.cpp" in files, True))
    commit_all(root, "rename")

    # A stamp commit on another branch, not an ancestor of HEAD: compared tree to tree.
    git(root, "checkout", "-q", "-b", "side", stamp)
    write(root, "src/server/QcSide.cpp", "int side;\n")
    side = commit_all(root, "side")
    git(root, "checkout", "-q", "main")
    line, files = module.analysis(root, side)
    results.append(check("stamp not an ancestor: still a selection", line.startswith("changed:"), True))
    results.append(check("file only on the stamp's branch drops out", "src/server/QcSide.cpp" in files, False))

    # From 40 % of the .cpp files on, every file.
    for index in range(2, 10):
        write(root, f"src/worldgen/QcFiller{index}.cpp", f"int filler{index} = 1;\n")
    line = module.analysis(root, stamp)[0]
    results.append(check("selection of 40 % or more: all", line.startswith("all (selection ≥ 40 %)"), True))
    return all(results)


def main():
    module = load_module(sys.argv[1])
    with tempfile.TemporaryDirectory() as root:
        root = os.path.realpath(root)
        git(root, "init", "-q", "-b", "main")
        for path, text in FILES.items():
            write(root, path, text)
        return 0 if run_cases(module, root) else 1


if __name__ == "__main__":
    sys.exit(main())
