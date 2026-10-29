#!/usr/bin/env python3
"""File selection of the static analysis in the full verify (decision E-929).

The full verify analyses with clang-tidy, clazy and qmllint only the sources that changed since the
last fully green full verify (its commit stands in build/.verify-full-last), plus the files
that include a changed header directly. Includes name the header by its path below src/ or tests/
(E-418), so `git grep` for `#include "<that path>"` finds them.
Limit: files that reach a changed header only through another header are not analysed again (the
closure over ten commits of 2026-10-29 took 890 of 1292 .cpp files, the direct includers 113); a
finding there that the compiler does not also warn about waits for --full-analysis. QML files that
use a changed QML component are not followed either (qmllint checks one file at a time).

Every source file is analysed again (the answer `all`) when
  - no commit is given (no green full verify recorded) or git does not know it;
  - the analysis setup changed since then: a .clang-tidy (root or tests/), .clazy*, the cmake/
    folder (compile flags), scripts/static-analysis.sh, scripts/verify-full-select.sh or this script;
  - a line of a CMakeLists.txt or *.cmake file that sets compile definitions, options or flags
    changed (COMPILE_SETTING below; other CMake changes, such as a new source in a list, do not);
  - the selection reaches SHARE_ALL of the .cpp files: the rest costs little more.
The stamp's preset hash and clang-tidy version are compared by scripts/verify-full-select.sh.
Deleted files drop out; a renamed file counts under its new path. The commit need not be an
ancestor of HEAD: the comparison is between its tree and the working tree.
The cheap checks (file and function length, output parameters, member access) are not selected
here: scripts/static-analysis.sh --cheap-all runs them over the whole tree.

  analysis --since <commit> --out <file>
      Writes the absolute paths of the selected .cpp/.h/.qml/.mm files to <file> (one per line,
      empty for `all`) and prints one line: `all: <reason>` or
      `changed: <n> files (<c> changed, <d> including a changed header), <t> of <total> .cpp files
      for clang-tidy and clazy, since <short commit>`.

Usage: scripts/verify-full-select.py analysis --since <commit> --out <file> [--root <dir>]
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = re.compile(r"^(src|tests)/.*\.(cpp|h|qml|mm)$")
# Changes that can change the findings in every file.
SETUP = re.compile(r"^((.*/)?\.clang-tidy|(.*/)?\.clazy.*|cmake/.*|scripts/static-analysis\.sh"
                   r"|scripts/verify-full-select\.(py|sh))$")
# CMake lines whose change can change the findings in many files.
COMPILE_SETTING = re.compile(r"compile_definitions|compile_options|add_definitions|CMAKE_CXX_FLAGS|"
                             r"CMAKE_CXX_STANDARD|compile_features", re.IGNORECASE)
CMAKE_FILES = [":(glob)**/CMakeLists.txt", ":(glob)**/*.cmake", ":(glob)CMakeLists.txt"]
# Share of the .cpp files from which every file is analysed.
SHARE_ALL = 0.4
# Include roots (E-418): the path in an include is relative to one of them.
INCLUDE_ROOTS = ("src/", "tests/")


def git(root, *arguments):
    result = subprocess.run(["git", *arguments], cwd=root, capture_output=True, text=True, check=False)
    return result.returncode, result.stdout


def changed_paths(root, since):
    """Paths that differ between the commit and the working tree (committed or not), and untracked
    ones; NUL-separated, so no file name is split."""
    paths = set()
    for command in (["diff", "--name-only", "-z", since], ["ls-files", "-z", "--others", "--exclude-standard"]):
        paths.update(path for path in git(root, *command)[1].split("\0") if path)
    return paths


def compile_setting_changes(root, since):
    """Changed lines of CMake files that set compile definitions, options or flags."""
    diff = git(root, "diff", "-U0", since, "--", *CMAKE_FILES)[1]
    return [line[1:].strip() for line in diff.splitlines()
            if line[:1] in "+-" and line[:3] not in ("+++", "---") and COMPILE_SETTING.search(line)]


def include_name(path):
    """The name an include gives the header: its path below the include root."""
    for prefix in INCLUDE_ROOTS:
        if path.startswith(prefix):
            return path[len(prefix):]
    return path


def includers(root, headers):
    """Tracked sources below src/ and tests/ that include one of the headers."""
    if not headers:
        return set()
    patterns = []
    for header in sorted(headers):
        patterns += ["-e", f'#include "{include_name(header)}"']
    status, output = git(root, "grep", "-l", "-F", *patterns, "--", "src", "tests")
    return set(output.split()) if status == 0 else set()


def dependants(root, changed):
    """The files that include a changed header directly (see the module comment for the limit)."""
    return includers(root, {path for path in changed if path.endswith(".h")}) - changed


def tidy_sources(root, files):
    """The .cpp files clang-tidy and clazy get: each .cpp, and the .cpp of the same name of a header
    (scripts/static-analysis.sh analyses a header through it)."""
    sources = set()
    for path in files:
        if path.endswith(".cpp"):
            sources.add(path)
        elif path.endswith(".h") and os.path.isfile(os.path.join(root, path[:-2] + ".cpp")):
            sources.add(path[:-2] + ".cpp")
    return sources


def tracked_sources(root):
    return [path for path in git(root, "ls-files", "src", "tests")[1].split() if path.endswith(".cpp")]


def analysis(root, since):
    """(summary line, selected relative paths); the paths are empty for `all`."""
    if not since:
        return "all: no green full verify recorded", []
    if git(root, "cat-file", "-e", f"{since}^{{commit}}")[0] != 0:
        return f"all: commit {since} of the last green full verify unknown to git", []

    paths = changed_paths(root, since)
    setup = sorted(path for path in paths if SETUP.match(path))
    if setup:
        return f"all: analysis setup changed since {since[:10]} ({', '.join(setup[:3])})", []

    settings = compile_setting_changes(root, since)
    if settings:
        return f"all: compile settings in CMake files changed since {since[:10]} ({settings[0][:60]})", []

    changed = {path for path in paths if SOURCE.match(path) and os.path.isfile(os.path.join(root, path))}
    extra = {path for path in dependants(root, changed) if SOURCE.match(path)}
    selected = sorted(changed | extra)
    tidy = len(tidy_sources(root, selected))
    total = len(tracked_sources(root))
    if total and tidy >= SHARE_ALL * total:
        return f"all (selection ≥ {round(SHARE_ALL * 100)} %): {tidy} of {total} .cpp files since {since[:10]}", []
    return (f"changed: {len(selected)} files ({len(changed)} changed, {len(extra)} including a changed header), "
            f"{tidy} of {total} .cpp files for clang-tidy and clazy, since {since[:10]}"), selected


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("command", choices=["analysis"])
    parser.add_argument("--since", default="")
    parser.add_argument("--out", required=True)
    parser.add_argument("--root", default=ROOT)
    arguments = parser.parse_args()

    root = os.path.abspath(arguments.root)
    line, selected = analysis(root, arguments.since)
    with open(arguments.out, "w", encoding="utf-8") as file:
        file.writelines(os.path.join(root, path) + "\n" for path in selected)
    print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
