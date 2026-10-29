#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Checks scripts/move-classes.py (package 80.2) in a throwaway git repository with a small project:
a class with its parts and a platform part, a header-only class, the only class of a library with
a single block, a QML file, tests with compile definitions, an alias, a shared set() of their
limit, a qc_add_test inside if(), and a fixture header move. Afterwards the library's blocks are
sorted (a new block in folder order, the emptied one gone), the includes, QML_FILES, the
translation's location and the tests' tests.cmake follow, a new topic has its include() line,
scripts/check-cmake-lists.py and `fix-includes.py --check` pass, and the warnings name the
__has_include path, the folder path in a compile definition, statements of two tests that end up
apart, old paths and an emptied folder in the docs; accept lines turn warnings into accepted ones
(exit code 2 while some remain, 0 when none). Bad lists (a class split from its part, a move into
another library, a file named twice, a renamed file, an untracked file), unknown options and a
tree with changes move nothing; a dry run changes nothing.
Usage: test_move_classes.py <path of move-classes.py>
"""

import os
import shutil
import subprocess
import sys
import tempfile

FILES = {
    "src/common/CMakeLists.txt": (
        "qt_add_library(qc_common STATIC)\n"
        "target_sources(qc_common PRIVATE\n"
        "    a/QcAlpha.h a/QcAlpha.cpp a/QcAlphaParts.cpp\n"
        "    a/QcBeta.h a/QcBeta.cpp\n"
        "    a/QcGamma.h\n"
        ")\n"
        "target_sources(qc_common PRIVATE\n"
        "    z/QcZeta.h z/QcZeta.cpp\n"
        ")\n"
        "if(APPLE)\n"
        "    target_sources(qc_common PRIVATE a/QcAlpha_mac.mm)\n"
        "endif()\n"
        "set_source_files_properties(qml/controls/Ui.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)\n"
        "qt_add_qml_module(qc_common\n"
        "    URI Sample\n"
        "    QML_FILES\n"
        "        qml/Main.qml\n"
        "        qml/controls/Lazy.qml\n"
        "        qml/controls/Ui.qml\n"
        "        qml/menus/Menu.qml\n"
        ")\n"),
    "src/server/CMakeLists.txt": "qt_add_library(qc_server STATIC)\ntarget_sources(qc_server PRIVATE\n    a/QcSolo.h a/QcSolo.cpp\n)\n",
    "src/server/a/QcSolo.h": "struct QcSolo {};\n",
    "src/server/a/QcSolo.cpp": '#include "server/a/QcSolo.h"\n',
    "src/common/a/QcAlpha.h": "struct QcAlpha {};\n",
    "src/common/a/QcAlpha.cpp": '#include "common/a/QcAlpha.h"\n',
    "src/common/a/QcAlphaParts.cpp": '#include "common/a/QcAlpha.h"\n',
    "src/common/a/QcAlpha_mac.mm": '#include "common/a/QcAlpha.h"\n',
    "src/common/a/QcBeta.h": '#include "common/a/QcAlpha.h"\n#include "common/a/QcGamma.h"\n',
    "src/common/a/QcBeta.cpp": '#include "common/a/QcBeta.h"\n',
    "src/common/a/QcGamma.h": "struct QcGamma {};\n",
    "src/common/z/QcZeta.h": '#include "common/a/QcGamma.h"\n',
    "src/common/z/QcZeta.cpp": '#include "common/z/QcZeta.h"\n',
    "src/common/qml/Main.qml": "Item {}\n",
    "src/common/qml/controls/Lazy.qml": "Item {}\n",
    "src/common/qml/controls/Ui.qml": "QtObject {}\n",
    "src/common/qml/menus/Menu.qml": "Item {}\n",
    "translations/qtcraft_de.ts": '<location filename="../src/common/a/QcAlpha.cpp" line="3"/>\n',
    "tests/CMakeLists.txt": (
        "# SPDX-FileCopyrightText: 2026 the QtCraft authors\n"
        "# SPDX-License-Identifier: GPL-3.0-or-later\n"
        "\n"
        "include(${CMAKE_CURRENT_SOURCE_DIR}/old/tests.cmake)\n"
        "include(${CMAKE_CURRENT_SOURCE_DIR}/other/tests.cmake)\n"
        "set_tests_properties(test_x PROPERTIES COST 5)\n"),
    "tests/QcFixture.h": '#include "common/a/QcAlpha.h"\n',
    "tests/old/tests.cmake": (
        "# Old topic.\n"
        "\n"
        "# The x test.\n"
        "qc_add_test(test_x SOURCES old/test_x.cpp QcFixture.h LIBRARIES qc_common)\n"
        "qc_add_test(test_y SOURCES old/test_y.cpp QcFixture.h LIBRARIES qc_common)\n"
        "target_compile_definitions(test_x PRIVATE\n"
        "    QTCRAFT_LAZY_DIR=\"${PROJECT_SOURCE_DIR}/src/common/qml/controls\")\n"
        "add_test(NAME test_x_scaled COMMAND test_x one)\n"
        "set_tests_properties(test_x_scaled PROPERTIES TIMEOUT 9)\n"
        "if(APPLE)\n"
        "    target_link_libraries(test_x PRIVATE Foundation)\n"
        "endif()\n"
        "set_tests_properties(test_x test_y PROPERTIES LABELS sample)\n"
        "if(WIN32)\n"
        "    target_link_libraries(test_y PRIVATE a)\n"
        "    target_link_libraries(test_x PRIVATE b)\n"
        "endif()\n"
        "if(MSVC)\n"
        "    set(qc_w_timeout 9)\n"
        "else()\n"
        "    set(qc_w_timeout 5)\n"
        "endif()\n"
        "qc_add_test(test_w SOURCES old/test_w.cpp LIBRARIES qc_common TIMEOUT ${qc_w_timeout})\n"
        "if(APPLE)\n"
        "    qc_add_test(test_v SOURCES old/test_v.cpp LIBRARIES qc_common)\n"
        "endif()\n"),
    "tests/other/tests.cmake": "# Other topic.\n\nqc_add_test(test_z SOURCES other/test_z.cpp LIBRARIES qc_common)\n",
    "tests/old/test_x.cpp": '#include "QcFixture.h"\n#include "common/a/QcBeta.h"\n',
    "tests/old/test_w.cpp": "int w;\n",
    "tests/old/test_v.cpp": "int v;\n",
    "tests/old/test_y.cpp": '#if __has_include("common/a/QcAlpha.h")\n#endif\n#include "QcFixture.h"\n',
    "tests/other/test_z.cpp": '#include "common/z/QcZeta.h"\n',
    "docs/Notes.md": "The class lives in src/common/a/QcAlpha.h.\nThe folder `src/common/a/` holds the samples.\n",
}
GOOD_PLAN = (
    "# sample\n"
    "src/common/a/QcAlpha.h\tsrc/common/n/QcAlpha.h\n"
    "src/common/a/QcAlpha.cpp\tsrc/common/n/QcAlpha.cpp\n"
    "src/common/a/QcAlphaParts.cpp\tsrc/common/n/QcAlphaParts.cpp\n"
    "src/common/a/QcAlpha_mac.mm\tsrc/common/n/QcAlpha_mac.mm\n"
    "src/common/a/QcBeta.h\tsrc/common/z/QcBeta.h\n"
    "src/common/a/QcBeta.cpp\tsrc/common/z/QcBeta.cpp\n"
    "src/common/a/QcGamma.h\tsrc/common/z/QcGamma.h\n"
    "src/common/qml/controls/Lazy.qml\tsrc/common/qml/frames/Lazy.qml\n"
    "src/server/a/QcSolo.h\tsrc/server/b/QcSolo.h\n"
    "src/server/a/QcSolo.cpp\tsrc/server/b/QcSolo.cpp\n"
    "tests/old/test_x.cpp\ttests/fresh/test_x.cpp\n"
    "tests/old/test_w.cpp\ttests/fresh/test_w.cpp\n"
    "tests/old/test_v.cpp\ttests/fresh/test_v.cpp\n"
    "tests/QcFixture.h\ttests/fixtures/QcFixture.h\n"
    "topic\ttests/fresh\tFresh tests.\n"
    "accept\tdocs/Notes.md\n"
    "accept\tQTCRAFT_LAZY_DIR\n"
)
BAD_PLANS = [
    ("src/common/a/QcAlpha.h\tsrc/common/n/QcAlpha.h\n", "would be split"),
    ("src/common/a/QcGamma.h\tsrc/server/a/QcGamma.h\n", "leaves its library"),
    ("src/common/a/QcGamma.h\tsrc/common/n/QcGamma.h\nsrc/common/a/QcGamma.h\tsrc/common/m/QcGamma.h\n", "already moved"),
    ("src/common/a/QcGamma.h\tsrc/common/n/QcDelta.h\n", "changes the file name"),
    ("tests/old/test_x.cpp\ttests/fresh/test_x.cpp\n", "needs a line 'topic"),
]
EXPECTED_COMMON = (
    "target_sources(qc_common PRIVATE\n"
    "    n/QcAlpha.h n/QcAlpha.cpp n/QcAlphaParts.cpp\n"
    ")\n"
    "target_sources(qc_common PRIVATE\n"
    "    z/QcBeta.h z/QcBeta.cpp\n"
    "    z/QcGamma.h\n"
    "    z/QcZeta.h z/QcZeta.cpp\n"
    ")\n"
    "if(APPLE)\n"
    "    target_sources(qc_common PRIVATE n/QcAlpha_mac.mm)\n"
    "endif()\n"
)
EXPECTED_QML = "        qml/Main.qml\n        qml/controls/Ui.qml\n        qml/frames/Lazy.qml\n        qml/menus/Menu.qml\n"
EXPECTED_FRESH = (
    "# The x test.\n"
    "qc_add_test(test_x SOURCES fresh/test_x.cpp fixtures/QcFixture.h LIBRARIES qc_common)\n"
    "target_compile_definitions(test_x PRIVATE\n"
    "    QTCRAFT_LAZY_DIR=\"${PROJECT_SOURCE_DIR}/src/common/qml/controls\")\n"
    "add_test(NAME test_x_scaled COMMAND test_x one)\n"
    "set_tests_properties(test_x_scaled PROPERTIES TIMEOUT 9)\n"
    "if(APPLE)\n"
    "    target_link_libraries(test_x PRIVATE Foundation)\n"
    "endif()\n"
    "set_tests_properties(test_x test_y PROPERTIES LABELS sample)\n"
)
EXPECTED_SHARED = (
    "if(MSVC)\n"
    "    set(qc_w_timeout 9)\n"
    "else()\n"
    "    set(qc_w_timeout 5)\n"
    "endif()\n"
    "qc_add_test(test_w SOURCES fresh/test_w.cpp LIBRARIES qc_common TIMEOUT ${qc_w_timeout})\n"
    "if(APPLE)\n"
    "    qc_add_test(test_v SOURCES fresh/test_v.cpp LIBRARIES qc_common)\n"
    "endif()\n"
)
EXPECTED_OLD = (
    "# Old topic.\n\n"
    "qc_add_test(test_y SOURCES old/test_y.cpp fixtures/QcFixture.h LIBRARIES qc_common)\n"
    "if(WIN32)\n"
    "    target_link_libraries(test_y PRIVATE a)\n"
    "    target_link_libraries(test_x PRIVATE b)\n"
    "endif()\n"
)


def git(root, *arguments):
    command = ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", *arguments]
    return subprocess.run(command, cwd=root, check=True, capture_output=True, text=True).stdout


def make_project(root, script):
    """The sample project as a git repository, with the tool and the scripts it imports in scripts/."""
    for path, text in FILES.items():
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8") as file:
            file.write(text)
    os.makedirs(os.path.join(root, "scripts"))
    for name in ("move-classes.py", "fix-includes.py", "check-cmake-lists.py"):
        shutil.copy(os.path.join(os.path.dirname(script), name), os.path.join(root, "scripts", name))
    git(root, "init", "-q")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "sample")


def run_tool(root, plan_text, *options, after=()):
    """Runs the tool from inside the project on a move list kept outside it (the tree stays clean)."""
    with tempfile.TemporaryDirectory() as folder:
        plan = os.path.join(folder, "plan.tsv")
        with open(plan, "w", encoding="utf-8") as file:
            file.write(plan_text)
        command = [sys.executable, os.path.join(root, "scripts", "move-classes.py"), *options, plan, *after]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def read(root, path):
    with open(os.path.join(root, path), encoding="utf-8") as file:
        return file.read()


class Checks:
    def __init__(self):
        self.failures = []

    def that(self, label, condition, detail=""):
        if not condition:
            self.failures.append(f"{label}{': ' + detail if detail else ''}")


def check_bad_plans(checks, root):
    for plan, expected in BAD_PLANS:
        code, output = run_tool(root, plan)
        checks.that(f"bad plan '{plan.strip()}' fails", code == 1, output)
        checks.that(f"bad plan '{plan.strip()}' names '{expected}'", expected in output, output)
        checks.that(f"bad plan '{plan.strip()}' changes nothing", git(root, "status", "--porcelain") == "")


def check_refusals(checks, root):
    """Unknown options, --root without a folder, an untracked file and a tree with changes move nothing."""
    for options, after, expected in ((("--dryrun",), (), "unknown option --dryrun"), ((), ("--root",), "--root needs a folder")):
        code, output = run_tool(root, GOOD_PLAN, *options, after=after)
        checks.that(f"'{expected}' refused", code == 1 and expected in output, output)
        checks.that(f"'{expected}' changes nothing", git(root, "status", "--porcelain") == "")
    with open(os.path.join(root, "src/common/a/QcNew.h"), "w", encoding="utf-8") as file:
        file.write("struct QcNew {};\n")
    code, output = run_tool(root, "src/common/a/QcNew.h\tsrc/common/n/QcNew.h\n", "--validate")
    checks.that("untracked file refused", code == 1 and "is not tracked by git" in output, output)
    os.remove(os.path.join(root, "src/common/a/QcNew.h"))
    with open(os.path.join(root, "docs/Notes.md"), "a", encoding="utf-8") as file:
        file.write("changed\n")
    code, output = run_tool(root, GOOD_PLAN)
    checks.that("tree with changes refused", code == 1 and "the tree has changes" in output, output)
    git(root, "checkout", "--", "docs/Notes.md")
    checks.that("refusals leave the tree clean", git(root, "status", "--porcelain") == "")


def check_dry_run(checks, root):
    code, output = run_tool(root, GOOD_PLAN, "--dry-run")
    checks.that("dry run changes nothing", git(root, "status", "--porcelain") == "", git(root, "status", "--porcelain"))
    checks.that("dry run lists the new block", "src/common/CMakeLists.txt: 3 source lines" in output, output)
    checks.that("dry run lists the test", "test_x: tests/old -> tests/fresh (6 statements)" in output, output)
    checks.that("dry run moves the shared set() with its test", "test_w: tests/old -> tests/fresh (2 statements)" in output, output)
    checks.that("dry run warns about __has_include", "names a file the run moves to src/common/n/QcAlpha.h" in output, output)
    checks.that("dry run reports with exit code 2 for warnings", code == 2, output)
    code, output = run_tool(root, GOOD_PLAN + "accept\tnothing like this\n", "--dry-run")
    checks.that("an unused accept line is a warning", "accept line 'nothing like this' matches no warning" in output, output)


def check_move(checks, root):
    code, output = run_tool(root, GOOD_PLAN)
    checks.that("the run reports its warnings with exit code 2", code == 2, output)
    checks.that("no check finding", "0 check finding(s)" in output, output)
    common = read(root, "src/common/CMakeLists.txt")
    checks.that("blocks regrouped, platform part rewritten", EXPECTED_COMMON in common, common)
    checks.that("QML_FILES sorted", EXPECTED_QML in common, common)
    server = read(root, "src/server/CMakeLists.txt")
    checks.that("single block replaced by the new folder's", server == "qt_add_library(qc_server STATIC)\n"
                "target_sources(qc_server PRIVATE\n    b/QcSolo.h b/QcSolo.cpp\n)\n", server)
    checks.that("include rewritten", read(root, "src/common/z/QcBeta.h") == '#include "common/n/QcAlpha.h"\n#include "common/z/QcGamma.h"\n',
                read(root, "src/common/z/QcBeta.h"))
    checks.that("fixture include rewritten", '#include "fixtures/QcFixture.h"' in read(root, "tests/fresh/test_x.cpp"))
    checks.that("translation location rewritten", "../src/common/n/QcAlpha.cpp" in read(root, "translations/qtcraft_de.ts"))
    fresh = read(root, "tests/fresh/tests.cmake")
    checks.that("new tests.cmake has the head", fresh.startswith("# SPDX-FileCopyrightText: 2026 the QtCraft authors\n"
                                                                 "# SPDX-License-Identifier: GPL-3.0-or-later\n\n# Fresh tests.\n"), fresh)
    checks.that("the test's statements moved", EXPECTED_FRESH in fresh, fresh)
    checks.that("shared set() and if()-test moved in order", fresh.endswith(EXPECTED_SHARED), fresh)
    old = read(root, "tests/old/tests.cmake")
    checks.that("the other test and the if() of both stay", old == EXPECTED_OLD, old)
    checks.that("warns about the moving statement of two tests", "tests/fresh/tests.cmake: 'set_tests_properties(test_x test_y "
                "PROPERTIES LABELS sample)' names tests that end up in another tests.cmake (test_y in tests/old)" in output, output)
    checks.that("warns about the staying statement of two tests", "tests/old/tests.cmake: 'if(WIN32)' names tests that end up "
                "in another tests.cmake (test_x in tests/fresh)" in output, output)
    lists = read(root, "tests/CMakeLists.txt")
    checks.that("include() line next to the old topic", "old/tests.cmake)\ninclude(${CMAKE_CURRENT_SOURCE_DIR}/fresh/tests.cmake)\n"
                "include(${CMAKE_CURRENT_SOURCE_DIR}/other" in lists, lists)
    checks.that("emptied folder removed", not os.path.exists(os.path.join(root, "src/common/a")))
    checks.that("new tests.cmake is staged", "tests/fresh/tests.cmake" in git(root, "ls-files"))
    checks.that("accepts the folder path", "accepted: tests/fresh/tests.cmake:11: names the folder src/common/qml/controls, "
                "which the run moved Lazy.qml out of" in output, output)
    checks.that("warns about __has_include", 'tests/old/test_y.cpp:1: __has_include("common/a/QcAlpha.h") names no file '
                "(the file is now src/common/n/QcAlpha.h)" in output, output)
    checks.that("accepts the old path in the docs", "accepted: docs/Notes.md:1: still names src/common/a/QcAlpha.h" in output, output)
    checks.that("names the emptied folder in the docs", "docs/Notes.md:2: names the folder src/common/a, which the run empties"
                in output, output)


def check_emptied_topic(checks, root):
    """A second run moves the last test of tests/other: its tests.cmake and include() line go."""
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "moved")
    code, output = run_tool(root, "tests/other/test_z.cpp\ttests/fresh/test_z.cpp\n"
                            'accept\t__has_include("common/a/QcAlpha.h")\n')
    checks.that("second run clean with its accept line", code == 0, output)
    checks.that("emptied tests.cmake removed", not os.path.exists(os.path.join(root, "tests/other")), output)
    lists = read(root, "tests/CMakeLists.txt")
    checks.that("its include() line removed", "other/tests.cmake" not in lists, lists)
    checks.that("the test appended", read(root, "tests/fresh/tests.cmake").endswith(
        "qc_add_test(test_z SOURCES fresh/test_z.cpp LIBRARIES qc_common)\n"))


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    script = os.path.abspath(sys.argv[1])
    checks = Checks()
    with tempfile.TemporaryDirectory() as root:
        make_project(root, script)
        check_bad_plans(checks, root)
        check_refusals(checks, root)
        check_dry_run(checks, root)
        check_move(checks, root)
        check_emptied_topic(checks, root)
    for failure in checks.failures:
        print(f"FAIL {failure}")
    if checks.failures:
        return 1
    print("test_move_classes: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
