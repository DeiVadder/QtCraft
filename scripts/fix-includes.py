#!/usr/bin/env python3
"""Keeps the project includes and source paths in step with where the files are (decision E-418).

Every project include names its header by its path below an include root: `src/` for the code
(`#include "common/blockrules/QcRails.h"`) and `tests/` for the shared test fixtures
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

The check also keeps the layers of src/ (decision E-419): a top folder includes headers of itself
and of the folders LAYERS allows it, nothing else (common nothing, worldgen and network only
common, ...; worlds also server, app the allow-list of LAYERS without server and headless (decision
E-705), headless anything). HEADER_LAYERS narrows the headers of a folder: the headers of worlds
include no server header. A new top folder needs its line.

Usage: scripts/fix-includes.py [--check] [--deps [<prefix>]] [--self-test]
  --check      change nothing; exit 1 and list the files that would change and the includes that
               cross a layer (verify.sh runs this)
  --deps       print which folders of src/ include headers of which other folders (with counts);
               with a prefix (e.g. worldgen) only the folders below it
  --self-test  checks bad includes in a temporary folder and fails if the layer check misses one
"""

import os
import re
import sys
import tempfile
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCLUDE_ROOTS = ("src", "tests")
CODE_SUFFIXES = (".h", ".cpp", ".mm")
SOURCE_SUFFIXES = (".h", ".cpp", ".mm", ".qml")
INCLUDE = re.compile(r'^(\s*#\s*include\s+)"([^"]+)"', re.MULTILINE)
# A file name in a CMake file: a relative path ending in a source suffix, without variables other
# than a leading ${PROJECT_SOURCE_DIR}/ or ${CMAKE_CURRENT_SOURCE_DIR}/.
CMAKE_PREFIXES = ("${PROJECT_SOURCE_DIR}/", "${CMAKE_CURRENT_SOURCE_DIR}/")
CMAKE_FILE = re.compile(r"(?<![\w${}/.-])((?:\$\{(?:PROJECT|CMAKE_CURRENT)_SOURCE_DIR\}/)?[\w./-]+\.(?:h|cpp|mm|qml))(?![\w])")
# A comment in a CMake file: a bracket comment (#[[ ... ]], #[=[ ... ]=], over several lines) or
# the rest of a line.
CMAKE_COMMENT = re.compile(r"#\[(=*)\[.*?\]\1\]|#[^\n]*", re.DOTALL)
TS_LOCATION = re.compile(r'(<location filename=")([^"]+)(")')
TRANSLATIONS = "translations"
# The top folders of src/ whose headers each top folder may include besides its own (decisions
# E-419, E-705); None: any. Direct includes only: audio reaches network through clientcore, but does
# not include it.
LAYERS = {
    "common": set(),
    "worldgen": {"common"},
    "network": {"common"},
    "clientcore": {"common", "network"},
    "audio": {"common", "clientcore"},
    "server": {"common", "network", "worldgen"},
    "worlds": {"common", "network", "worldgen", "server"},
    "app": {"common", "network", "worldgen", "clientcore", "audio", "worlds"},
    "headless": None,
}
# Narrower layers for the headers of a top folder (decision E-705): the headers of worlds include no
# server header, so app, which includes them, sees no server type; the sources of worlds may.
HEADER_LAYERS = {
    "worlds": {"common", "network", "worldgen"},
}


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

    Relative paths and ${CMAKE_CURRENT_SOURCE_DIR}/ count from the folder of the CMakeLists.txt
    that holds them or includes them (a `*.cmake` file counts from the nearest folder above it with
    a CMakeLists.txt); a rewritten path keeps its prefix. Comments (also bracket comments) stay as
    they are."""
    folder = os.path.dirname(os.path.join(ROOT, path))
    while not path.endswith("CMakeLists.txt") and not os.path.exists(os.path.join(folder, "CMakeLists.txt")):
        folder = os.path.dirname(folder)

    def replace(match):
        token = match.group(1)
        prefix = next((p for p in CMAKE_PREFIXES if token.startswith(p)), "")
        base = ROOT if prefix == CMAKE_PREFIXES[0] else folder
        target = by_name.get(os.path.basename(token))
        if os.path.exists(os.path.join(base, token[len(prefix):])) or target is None:
            return token
        if prefix == CMAKE_PREFIXES[0]:
            return prefix + target
        return prefix + os.path.relpath(os.path.join(ROOT, target), folder)

    parts = []
    position = 0
    for comment in CMAKE_COMMENT.finditer(text):
        parts.append(CMAKE_FILE.sub(replace, text[position:comment.start()]) + comment.group(0))
        position = comment.end()
    parts.append(CMAKE_FILE.sub(replace, text[position:]))
    return "".join(parts)


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


def layer_violations(root, files):
    """The includes of the given src/ files (project-relative paths below `root`) that cross a layer.

    An include counts by its first path part (`server/...` is the server's), the layers of files
    in a top folder missing from LAYERS are reported as such."""
    problems = []
    for path in files:
        layer = include_path(path)[1].partition("/")[0]
        if layer not in LAYERS:
            problems.append(f"{path}: top folder src/{layer} has no line in LAYERS (decision E-419)")
            continue
        narrowed = path.endswith(".h") and layer in HEADER_LAYERS
        allowed = HEADER_LAYERS[layer] if narrowed else LAYERS[layer]
        if allowed is None:
            continue
        with open(os.path.join(root, path), encoding="utf-8") as file:
            text = file.read()
        for match in INCLUDE.finditer(text):
            target = match.group(2).partition("/")[0]
            if "/" in match.group(2) and target in LAYERS and target != layer and target not in allowed:
                kind = "headers of src/" if narrowed else "src/"
                problems.append(f"{path}: includes {match.group(2)}, but {kind}{layer} may include only "
                                f"{', '.join(sorted(allowed)) or 'itself'} (decisions E-419, E-705)")
    return problems


# Bad samples of the self-test: (file below src/, its text, expected part of the finding).
SELF_TEST_CASES = [
    ("common/core/QcA.h", '#include "server/core/QcServer.h"\n', "src/common may include only itself"),
    ("worldgen/a/QcA.cpp", '#include "common/QcB.h"\n#include "network/QcC.h"\n', "includes network/QcC.h"),
    ("clientcore/a/QcA.cpp", '#include "server/QcS.h"\n', "src/clientcore may include only common, network"),
    ("audio/QcA.cpp", '  #  include "network/QcN.h"\n', "includes network/QcN.h"),
    ("server/QcA.cpp", '#include "app/QcGameController.h"\n', "src/server may include only common, network, worldgen"),
    ("plugins/QcA.cpp", '#include "common/QcB.h"\n', "top folder src/plugins has no line in LAYERS"),
    ("app/QcA.cpp", '#include "worlds/QcW.h"\n#include "server/QcS.h"\n', "includes server/QcS.h"),
    ("worlds/QcA.cpp", '#include "clientcore/QcC.h"\n', "src/worlds may include only common, network, server, worldgen"),
    ("server/QcB.cpp", '#include "worlds/QcW.h"\n', "src/server may include only common, network, worldgen"),
    ("worlds/QcB.h", '#include "server/QcS.h"\n', "headers of src/worlds may include only common, network, worldgen"),
]
SELF_TEST_GOOD = [
    ("common/a/QcA.cpp", '#include "common/b/QcB.h"\n#include "QcLocal.h"\n#include <QList>\n#include "moc_QcA.cpp"\n'),
    ("server/QcA.cpp", '#include "worldgen/QcW.h"\n#include "network/QcN.h"\n#include "common/QcC.h"\n'),
    ("app/QcA.cpp", '#include "worlds/QcW.h"\n#include "audio/QcAudio.h"\n'),
    ("worlds/QcA.cpp", '#include "server/QcS.h"\n#include "worldgen/QcW.h"\n'),
    ("audio/QcA.cpp", '#include "clientcore/sound/QcSoundBank.h"\n'),
]


# A CMake text in tests/CMakeLists.txt naming a moved file (the real tests/world/QcVillageTestUtil.h)
# in code and in comments, and the text the rewrite should give.
SELF_TEST_CMAKE = (
    "set(a ${CMAKE_CURRENT_SOURCE_DIR}/old/QcVillageTestUtil.h old/QcVillageTestUtil.h)\n"
    "#[[ old/QcVillageTestUtil.h\n  old/QcVillageTestUtil.h ]] set(b old/QcVillageTestUtil.h) # old/QcVillageTestUtil.h\n"
    "#[=[ ${PROJECT_SOURCE_DIR}/old/QcVillageTestUtil.h ]=]\n",
    "set(a ${CMAKE_CURRENT_SOURCE_DIR}/world/QcVillageTestUtil.h world/QcVillageTestUtil.h)\n"
    "#[[ old/QcVillageTestUtil.h\n  old/QcVillageTestUtil.h ]] set(b world/QcVillageTestUtil.h) # old/QcVillageTestUtil.h\n"
    "#[=[ ${PROJECT_SOURCE_DIR}/old/QcVillageTestUtil.h ]=]\n",
)


def self_test():
    """Runs the layer check on the bad samples and the good ones and the CMake rewrite on its
    sample; 0 when they find and give exactly what they should."""
    failures = []
    rewritten = rewrite_cmake("tests/CMakeLists.txt", SELF_TEST_CMAKE[0], index_files())
    if rewritten != SELF_TEST_CMAKE[1]:
        failures.append(f"CMake rewrite gave:\n{rewritten}")
    for relative, text, expected in SELF_TEST_CASES:
        with tempfile.TemporaryDirectory() as root:
            path = write_sample(root, relative, text)
            found = layer_violations(root, [path])
            if not any(expected in problem for problem in found):
                failures.append(f"missed in {relative}: expected '{expected}', found {found}")
    with tempfile.TemporaryDirectory() as root:
        paths = [write_sample(root, relative, text) for relative, text in SELF_TEST_GOOD]
        found = layer_violations(root, paths)
        if found:
            failures.append(f"findings in the good samples: {found}")
    for failure in failures:
        print(f"fix-includes self-test: {failure}")
    if not failures:
        print(f"fix-includes self-test: {len(SELF_TEST_CASES)} bad samples found, good samples clean")
    return 1 if failures else 0


def write_sample(root, relative, text):
    """Writes a sample file below root/src; its path relative to root."""
    path = os.path.join("src", relative)
    os.makedirs(os.path.dirname(os.path.join(root, path)), exist_ok=True)
    with open(os.path.join(root, path), "w", encoding="utf-8") as file:
        file.write(text)
    return path


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
    if "--self-test" in arguments:
        return self_test()
    by_name = index_files()
    if "--deps" in arguments:
        position = arguments.index("--deps")
        prefix = arguments[position + 1] if position + 1 < len(arguments) else ""
        print_dependencies(by_name, prefix.rstrip("/"))
        return 0

    changes, problems = planned_changes(by_name)
    problems += layer_violations(ROOT, walk("src", CODE_SUFFIXES))
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
