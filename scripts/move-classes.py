#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Moves classes, QML files and tests to other folders of their library by a move list (package 80.2,
docs/Neugliederung-Plan.md; decisions E-417, E-418).

A move list is a tab-separated text file, one file per line (paths relative to the project):
    src/<library>/<old folder>/QcSample.h<TAB>src/<library>/<new folder>/QcSample.h
    tests/<old topic>/test_sample.cpp<TAB>tests/<new topic>/test_sample.cpp
    topic<TAB>tests/<new topic><TAB>What the tests of the new topic check.
    accept<TAB><part of a warning>
Empty lines and lines starting with `#` are comments. A `topic` line gives the description of a
tests.cmake that the move creates (first comment line of the new file). An `accept` line marks the
warnings containing its text as known follow-ups of the package: they are printed, but do not count;
an accept line that matches no warning is a warning itself. Every old path must be tracked by git,
and a run refuses to start on a tree with changes (a failed run is undone with
`git reset --hard && git clean -fd`). File names never change: a
move only changes the folder, and the folder stays in its library (src/<library>/...; tests/...). A
header moves with every part of its class (`QcName.cpp`, `QcNameParts.cpp`, `QcName_mac.mm`; a part
belongs to the longest header name of its folder it starts with, as QcWorldRendererShadows.cpp to
QcWorldRenderer.h), and a part never moves without its header.

One run
  1. moves the line of each moved source in the CMake file of its library from the target_sources
     block of its old folder into the block of its new folder (alphabetically; a new block at its
     place in folder order, an emptied block is removed);
  2. moves the statements of each moved test from its tests.cmake into tests/<topic>/tests.cmake:
     the qc_add_test line with its comment and every later command of the same test
     (target_compile_definitions, add_dependencies, qt_add_shaders, an add_test alias and its
     set_tests_properties, an if() block that only concerns this test), and a set() of a variable
     only these tests use; a new tests.cmake gets the project's head and an include() line in
     tests/CMakeLists.txt next to the old one, an emptied one is removed with its include() line;
  3. runs `git mv` for every file and removes emptied folders;
  4. runs scripts/fix-includes.py: includes, the remaining paths in the CMake files (QML_FILES,
     set_source_files_properties, REQUIRED_FILES, platform blocks) and the translations' locations;
  5. sorts QML_FILES and the source lists of qt_add_executable/qt_add_library that a move touched;
  6. warns about what it cannot rewrite: `__has_include("...")` paths that no longer exist, folder
     paths in CMake files and sources naming a folder files left (a compile definition such as
     QTCRAFT_QML_CONTROLS_DIR), test statements naming tests that end up in different tests.cmake
     files (`set_tests_properties(a b ...)`, an if() block of two tests), and old paths of files and
     of emptied folders left anywhere else in the project (docs, scripts, comments; the history in
     docs/Entscheidungen.md, docs/Log.md, docs/FORTSCHRITT.md, docs/archiv and the plan stay as
     they are);
  7. runs scripts/check-cmake-lists.py and `scripts/fix-includes.py --check` on the result.
It commits nothing. The QML type registration needs no list of folders: src/app/CMakeLists.txt
derives the include folders of the generated qc_app_core_qmltyperegistrations.cpp from the sources.

Usage: scripts/move-classes.py [--dry-run] [--validate] [--root <dir>] <move list> [<move list> ...]
  --dry-run   change nothing: check the lists, print the moves, the CMake lines and test statements
              that would move, the number of files whose includes change, and the warnings
  --validate  only check the lists: every file exists, is named once (also across the lists), keeps
              its name and library and moves with the parts of its class; exit 1 on a problem
  --root      the project folder (default: the folder above scripts/; the self-test uses a
              throwaway tree); a note when the current folder lies outside it
Exit code 0: moved (or would move) cleanly; 2: moved (or would move), but warnings not accepted by
the list remain; 1: a problem with a list or the tree (nothing moved), or a check of the result
failed.
"""

import importlib.util
import os
import re
import subprocess
import sys

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
SOURCE_SUFFIXES = (".h", ".cpp", ".mm", ".qml")
CODE_SUFFIXES = (".h", ".cpp", ".mm")
TOPIC_HEAD = ("# Included by tests/CMakeLists.txt (decision E-417): paths are relative to tests/, and the test\n"
              "# properties at the end of that file (labels, costs, required files) apply to these tests.\n")
# History and plans keep the old paths (docs/Neugliederung-Plan.md, section 4).
HISTORY = ("docs/Entscheidungen.md", "docs/Entscheidungen-Index.md", "docs/Log.md", "docs/FORTSCHRITT.md",
           "docs/Neugliederung-Plan.md", "docs/archiv/", "docs/Code-Karte.md", "scripts/restructure/",
           "scripts/move-classes.py", "tests/scripts/test_move_classes.py")
RECOVERY = "undo the run with: git reset --hard && git clean -fd"
OPTIONS = ("--dry-run", "--validate", "--root")
TEXT_SUFFIXES = (".h", ".cpp", ".mm", ".qml", ".txt", ".cmake", ".md", ".py", ".sh", ".json", ".js", ".ps1",
                 ".yml", ".yaml", ".in", ".toml", ".html", ".css")
HAS_INCLUDE = re.compile(r'__has_include\s*\(\s*"([^"]+)"\s*\)')
FOLDER_PATH = re.compile(r"\$\{(PROJECT_SOURCE_DIR|CMAKE_CURRENT_SOURCE_DIR)\}/([\w./-]+)")
BLOCK_START = re.compile(r"^target_sources\((\S+) PRIVATE\s*$")
COMMAND = re.compile(r"^\s*(\w+)\s*\(")
TEST_INCLUDE = re.compile(r"^include\(\$\{CMAKE_CURRENT_SOURCE_DIR\}/([\w-]+)/tests\.cmake\)\s*$")


def load(name, file_name):
    """Imports a sibling script (its file name has a dash) as a module."""
    spec = importlib.util.spec_from_file_location(name, os.path.join(SCRIPTS, file_name))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


FIX = load("fix_includes", "fix-includes.py")
CHECK = load("check_cmake_lists", "check-cmake-lists.py")


class Plan:
    """The moves of one run: old path -> new path, and the descriptions of new test topics."""

    def __init__(self):
        self.moves = {}
        self.topics = {}
        self.problems = []
        self.origin = {}
        self.accepts = []

    def read(self, path):
        """Adds the lines of a move list; a file named twice (also in another list) is a problem."""
        with open(path, encoding="utf-8") as file:
            for number, line in enumerate(file, 1):
                fields = line.rstrip("\n").split("\t")
                if not fields[0].strip() or fields[0].startswith("#"):
                    continue
                where = f"{path}:{number}"
                if fields[0] == "topic" and len(fields) == 3:
                    self.topics[fields[1].strip("/")] = fields[2]
                elif fields[0] == "accept" and len(fields) == 2 and fields[1]:
                    self.accepts.append((fields[1], where))
                elif len(fields) == 2:
                    self.add(fields[0], fields[1], where)
                else:
                    self.problems.append(f"{where}: expected '<old path><TAB><new path>', 'topic<TAB><folder><TAB><text>' "
                                         "or 'accept<TAB><text>'")

    def add(self, old, new, where):
        if old in self.moves:
            self.problems.append(f"{where}: {old} is already moved by {self.origin[old]}")
            return
        if new in self.moves.values():
            self.problems.append(f"{where}: two files move to {new}")
        self.moves[old] = new
        self.origin[old] = where


def library(path):
    """The library of a project path: src/<top folder> or tests."""
    parts = path.split("/")
    if parts[0] == "src" and len(parts) > 2:
        return "src/" + parts[1]
    return parts[0]


def class_parts(folder_files):
    """Header name -> the files of its class in one folder (header first): every source whose stem
    starts with the header's stem and with no longer header stem of the folder."""
    headers = sorted((os.path.splitext(name)[0] for name in folder_files if name.endswith(".h")), key=len, reverse=True)
    parts = {stem + ".h": [stem + ".h"] for stem in headers}
    for name in sorted(folder_files):
        if name.endswith(".h") or not name.endswith(CODE_SUFFIXES):
            continue
        stem = os.path.splitext(name)[0]
        owner = next((h for h in headers if stem.startswith(h)), None)
        if owner:
            parts[owner + ".h"].append(name)
    return parts


def validate(plan, root):
    """The problems of the moves: missing files, taken targets, renames, other libraries, split classes."""
    problems = list(plan.problems)
    tracked = set(tracked_files(root))
    for old, new in sorted(plan.moves.items()):
        where = plan.origin[old]
        if not os.path.isfile(os.path.join(root, old)):
            problems.append(f"{where}: {old} does not exist")
        elif old not in tracked:
            problems.append(f"{where}: {old} is not tracked by git (git mv needs it)")
        if os.path.exists(os.path.join(root, new)):
            problems.append(f"{where}: {new} exists already")
        if os.path.basename(old) != os.path.basename(new):
            problems.append(f"{where}: {old} -> {new} changes the file name")
        if library(old) != library(new) or library(old) not in ("tests",) and not library(old).startswith("src/"):
            problems.append(f"{where}: {old} -> {new} leaves its library ({library(old)})")
        if os.path.dirname(old) == os.path.dirname(new):
            problems.append(f"{where}: {old} stays in its folder")
    problems += split_classes(plan, root)
    return problems


def split_classes(plan, root):
    """Problems for classes whose header and parts do not move together into one folder."""
    problems = []
    folders = sorted({os.path.dirname(old) for old in plan.moves})
    for folder in folders:
        full = os.path.join(root, folder)
        if not os.path.isdir(full):
            continue
        for header, files in class_parts(os.listdir(full)).items():
            paths = [f"{folder}/{name}" for name in files]
            targets = {os.path.dirname(plan.moves[p]) if p in plan.moves else folder for p in paths}
            if len(targets) > 1:
                where = plan.origin.get(f"{folder}/{header}") or plan.origin[next(p for p in paths if p in plan.moves)]
                problems.append(f"{where}: the class {folder}/{header} would be split: "
                                + ", ".join(f"{os.path.basename(p)} -> {os.path.dirname(plan.moves.get(p, p))}" for p in paths))
    return problems


# ---------------------------------------------------------------------------------------------
# Source blocks of the libraries (src/<library>/CMakeLists.txt)


class Block:
    """A top-level `target_sources(<target> PRIVATE` block of a CMake file, one entry per line."""

    def __init__(self, target, entries):
        self.target = target
        self.entries = entries

    def folder(self):
        return os.path.dirname(self.entries[0].split()[0]) if self.entries else None

    def render(self):
        return [f"target_sources({self.target} PRIVATE"] + self.entries + [")"]


def parse_blocks(text):
    """The lines of a CMake file as a list of plain lines and Blocks (top level only, outside if())."""
    items = []
    lines = text.split("\n")
    depth = 0
    index = 0
    while index < len(lines):
        line = lines[index]
        command = COMMAND.match(line)
        name = command.group(1).lower() if command else ""
        depth += 1 if name == "if" else -1 if name == "endif" else 0
        start = BLOCK_START.match(line)
        if start and depth == 0:
            end = lines.index(")", index)
            items.append(Block(start.group(1), lines[index + 1:end]))
            index = end + 1
            continue
        items.append(line)
        index += 1
    return items


def render_blocks(items):
    lines = []
    for item in items:
        lines += item.render() if isinstance(item, Block) else [item]
    return "\n".join(lines)


def entry_key(entry):
    return entry.split()[0].lower()


def insert_entry(items, target, folder, entry):
    """Puts an entry into the block of its folder, a new block at its place in folder order if needed."""
    blocks = [item for item in items if isinstance(item, Block) and item.target == target]
    home = next((b for b in blocks if b.folder() == folder), None)
    if home:
        home.entries.append(entry)
        home.entries.sort(key=entry_key)
        return
    new = Block(target, [entry])
    later = next((b for b in blocks if b.entries and b.folder().lower() > folder.lower()), None)
    if later:
        position = items.index(later)
    else:
        position = items.index(blocks[-1]) + 1 if blocks else len(items)
    items.insert(position, new)


def move_block_lines(cmake_path, moves, problems):
    """(new text, number of moved lines) of a library's CMake file with the lines of moved files in the
    blocks of their new folders; moves map paths relative to the CMake file's folder."""
    with open(cmake_path, encoding="utf-8") as file:
        text = file.read()
    items = parse_blocks(text)
    queued = []
    for block in [item for item in items if isinstance(item, Block)]:
        kept = []
        for entry in block.entries:
            files = entry.split()
            moved = [name for name in files if name in moves]
            if not moved:
                kept.append(entry)
                continue
            if len(moved) != len(files) or len({os.path.dirname(moves[n]) for n in files}) != 1:
                problems.append(f"{cmake_path}: the line '{entry.strip()}' moves only in part")
                kept.append(entry)
                continue
            indent = entry[:len(entry) - len(entry.lstrip())]
            queued.append((block.target, os.path.dirname(moves[files[0]]), indent + " ".join(moves[n] for n in files)))
        block.entries = kept
    for target, folder, entry in queued:
        insert_entry(items, target, folder, entry)
    items = [item for item in items if not (isinstance(item, Block) and not item.entries)]
    return render_blocks(items), len(queued)


def library_cmake_moves(plan, root):
    """CMake file of a library -> {old path relative to it: new path relative to it} for moved sources."""
    result = {}
    for old, new in plan.moves.items():
        lib = library(old)
        if not lib.startswith("src/"):
            continue
        cmake = os.path.join(root, lib, "CMakeLists.txt")
        result.setdefault(cmake, {})[os.path.relpath(old, lib)] = os.path.relpath(new, lib)
    return result


# ---------------------------------------------------------------------------------------------
# Test statements (tests/<topic>/tests.cmake)


QC_ADD_TEST = re.compile(r"\bqc_add_test\(\s*(\w+)\s+SOURCES\s+(\S+)")


class Statement:
    """A command (or a whole if() block) of a tests.cmake with the comment lines directly above it;
    a free statement holds only comment or blank lines that belong to no command."""

    def __init__(self, comments, lines, free=False):
        self.comments = comments
        self.lines = lines
        self.free = free
        self.owner = None
        self.variable_users = None
        self.named = set()

    def code(self):
        return "\n".join(CHECK.strip_comment(line) for line in self.lines) if not self.free else ""

    def command(self):
        match = COMMAND.match(self.code())
        return match.group(1).lower() if match else ""

    def arguments(self):
        code = self.code()
        return code.split("(", 1)[1].replace(")", " ").split() if "(" in code else []


def parse_statements(text):
    """(head, statements) of a tests.cmake: the head is everything up to the first blank line before
    the first command. Comment lines directly above a command belong to it; a blank line ends their
    claim (such comments and the blank lines are free statements)."""
    lines = text.rstrip("\n").split("\n")
    head, statements, comments = [], [], []
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.lstrip().startswith("#"):
            comments.append(line)
            index += 1
            continue
        if not line.strip():
            if statements:
                statements.append(Statement([], comments + [line], free=True))
            else:
                head += comments + [line]
            comments = []
            index += 1
            continue
        end = statement_end(lines, index)
        statements.append(Statement(comments, lines[index:end]))
        comments = []
        index = end
    if comments:
        statements.append(Statement([], comments, free=True))
    return head, statements


def statement_end(lines, start):
    """The index after a command starting at `start`: after its closing parenthesis, or after the
    matching endif() of an if()."""
    match = COMMAND.match(lines[start])
    is_if = bool(match) and match.group(1).lower() == "if"
    depth = 0
    if_depth = 0
    index = start
    while index < len(lines):
        code = CHECK.strip_comment(lines[index])
        command = COMMAND.match(code)
        name = command.group(1).lower() if command else ""
        if_depth += 1 if name == "if" else -1 if name == "endif" else 0
        depth += code.count("(") - code.count(")")
        index += 1
        if depth <= 0 and (not is_if or if_depth == 0):
            return index
    return index


def assign_owners(statements):
    """Sets the test each statement belongs to (None: shared or unknown) and, for a set() without
    owner, the tests that use its variables."""
    tests = {match.group(1) for s in statements for match in QC_ADD_TEST.finditer(s.code())}
    aliases = {}
    for statement in statements:
        arguments = statement.arguments()
        if statement.command() == "add_test" and "NAME" in arguments and "COMMAND" in arguments:
            aliases[arguments[arguments.index("NAME") + 1]] = arguments[arguments.index("COMMAND") + 1]
    for statement in statements:
        statement.owner = statement_owner(statement, tests, aliases)
        words = set(re.findall(r"[\w-]+", statement.code()))
        statement.named = {aliases.get(word, word) for word in words if aliases.get(word, word) in tests}
    for statement in statements:
        names = set(re.findall(r"\bset\(\s*(\w+)", statement.code()))
        if statement.owner or not names:
            continue
        statement.variable_users = {s.owner for s in statements
                                    if s is not statement and any("${" + n + "}" in s.code() for n in names)}


def statement_owner(statement, tests, aliases):
    """The test a statement belongs to: the test it defines, configures or aliases (an if() block:
    the only test it names)."""
    if statement.free:
        return None
    arguments = statement.arguments()
    if statement.command() == "if":
        words = set(re.findall(r"[\w-]+", statement.code()))
        found = {aliases.get(word, word) for word in words if aliases.get(word, word) in tests}
        return found.pop() if len(found) == 1 else None
    if statement.command() == "add_test" and "NAME" in arguments:
        name = aliases.get(arguments[arguments.index("NAME") + 1])
        return name if name in tests else None
    first = aliases.get(arguments[0], arguments[0]) if arguments else ""
    return first if first in tests else None


def render_statements(head, statements):
    lines = list(head)
    for statement in statements:
        lines += statement.comments + statement.lines
    while lines and not lines[-1].strip():
        lines.pop()
    return re.sub(r"\n{3,}", "\n\n", "\n".join(lines) + "\n")


def test_moves(plan):
    """Path below tests/ -> (old topic, new topic) of every moved file of a topic folder."""
    moves = {}
    for old, new in plan.moves.items():
        old_parts, new_parts = old.split("/"), new.split("/")
        if library(old) == "tests" and len(old_parts) == 3 and len(new_parts) == 3 and old_parts[1] != new_parts[1]:
            moves["/".join(old_parts[1:])] = (old_parts[1], new_parts[1])
    return moves


class TestFiles:
    """The tests.cmake files of one run: their statements, which tests move, which files are new."""

    def __init__(self, root, plan):
        self.root = root
        self.plan = plan
        self.files = {}
        self.created = []
        self.problems = []
        self.warnings = []
        self.moved = []
        self.anchors = {}

    def get(self, topic):
        """(head, statements) of tests/<topic>/tests.cmake, a new head for a new file."""
        if topic not in self.files:
            path = os.path.join(self.root, "tests", topic, "tests.cmake")
            if os.path.exists(path):
                with open(path, encoding="utf-8") as file:
                    head, statements = parse_statements(file.read())
            else:
                head, statements = self.new_head(topic), []
                self.created.append(topic)
            assign_owners(statements)
            self.files[topic] = (head, statements)
        return self.files[topic]

    def new_head(self, topic):
        with open(os.path.join(self.root, "tests", "CMakeLists.txt"), encoding="utf-8") as file:
            spdx = file.read().split("\n")[:2]
        text = self.plan.topics.get(f"tests/{topic}")
        if text is None:
            self.problems.append(f"tests/{topic}/tests.cmake is new: the move list needs a line 'topic<TAB>tests/{topic}<TAB><text>'")
            text = f"Tests of {topic}."
        return spdx + ["", "# " + text] + TOPIC_HEAD.rstrip("\n").split("\n") + [""]

    def move_tests(self):
        """Moves the statements of every test whose first source moves into the tests.cmake of its
        new topic, in the order of the old file."""
        moves = test_moves(self.plan)
        moving = {}
        for topic in sorted({old for old, _ in moves.values()}):
            if not os.path.exists(os.path.join(self.root, "tests", topic, "tests.cmake")):
                continue
            for statement in list(self.get(topic)[1]):
                for name, source in QC_ADD_TEST.findall(statement.code()):
                    if source in moves:
                        moving[name] = (topic, moves[source][1], source)
        found = {source for _, _, source in moving.values()}
        for path in sorted(set(moves) - found):
            if os.path.basename(path).startswith("test_") and path.endswith(".cpp"):
                self.warnings.append(f"no qc_add_test in tests/{moves[path][0]}/tests.cmake starts with {path}; "
                                     "its statements stay where they are")
        for name, (old_topic, new_topic, _) in moving.items():
            self.anchors.setdefault(new_topic, old_topic)
            self.move_one(name, old_topic, new_topic, moving)
        self.check_shared(moving)

    def check_shared(self, moving):
        """Warns about statements that name tests which end up in another tests.cmake than the
        statement (`set_tests_properties(a b ...)`, an if() block of two tests)."""
        home = {}
        for topic, (_, statements) in self.files.items():
            for statement in statements:
                for name, _ in QC_ADD_TEST.findall(statement.code()):
                    home[name] = topic
        for topic, (_, statements) in sorted(self.files.items()):
            for statement in statements:
                elsewhere = sorted(n for n in statement.named if home.get(n, topic) != topic)
                if elsewhere and (statement.named & set(moving)):
                    where = ", ".join(f"{n} in tests/{home[n]}" for n in elsewhere)
                    self.warnings.append(f"tests/{topic}/tests.cmake: '{statement.lines[0].strip()}' names tests that end up "
                                         f"in another tests.cmake ({where}); split it by hand")

    def move_one(self, test, old_topic, new_topic, moving):
        """Moves the statements of one test (and set()s only tests of the same new topic use)."""
        _, source = self.get(old_topic)
        _, target = self.get(new_topic)
        owned = []
        for statement in source:
            if statement.owner == test:
                owned.append(statement)
            elif statement.variable_users and test in statement.variable_users:
                if all(moving.get(user, (None, None))[1] == new_topic for user in statement.variable_users):
                    owned.append(statement)
                else:
                    self.warnings.append(f"tests/{old_topic}/tests.cmake: '{statement.lines[0].strip()}' sets a variable "
                                         f"{test} uses, and tests that stay or move elsewhere use it too: it stays; "
                                         "check the order of the include() lines")
        for statement in owned:
            source.remove(statement)
            target.append(statement)
        self.moved.append(f"{test}: tests/{old_topic} -> tests/{new_topic} ({len(owned)} statements)")

    def emptied_topics(self):
        """The topics whose tests.cmake keeps no command after the moves."""
        return sorted(topic for topic, (_, statements) in self.files.items() if all(s.free for s in statements))

    def write(self):
        """Writes the changed tests.cmake files and tests/CMakeLists.txt, removes the emptied ones."""
        emptied = self.emptied_topics()
        for topic, (head, statements) in sorted(self.files.items()):
            path = os.path.join(self.root, "tests", topic, "tests.cmake")
            if topic in emptied:
                if os.path.exists(path):
                    subprocess.run(["git", "rm", "-q", "-f", os.path.relpath(path, self.root)], cwd=self.root, check=True)
                continue
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8") as file:
                file.write(render_statements(head, statements))
        if self.files:
            self.update_includes(emptied)
        return emptied

    def update_includes(self, emptied):
        """Adds the include() lines of new topics after the line of the topic their first test came
        from (several in the order they were made) and removes the lines of emptied topics."""
        path = os.path.join(self.root, "tests", "CMakeLists.txt")
        with open(path, encoding="utf-8") as file:
            lines = file.read().split("\n")
        for topic in self.created:
            if topic in emptied:
                continue
            topics = [(i, TEST_INCLUDE.match(line).group(1)) for i, line in enumerate(lines) if TEST_INCLUDE.match(line)]
            anchor = self.anchors.get(topic)
            after = next((i for i, t in topics if t == anchor), topics[-1][0])
            while after + 1 < len(lines) and TEST_INCLUDE.match(lines[after + 1]) and \
                    TEST_INCLUDE.match(lines[after + 1]).group(1) in self.created:
                after += 1
            lines.insert(after + 1, "include(${CMAKE_CURRENT_SOURCE_DIR}/" + topic + "/tests.cmake)")
        lines = [line for line in lines if not (TEST_INCLUDE.match(line) and TEST_INCLUDE.match(line).group(1) in emptied)]
        with open(path, "w", encoding="utf-8") as file:
            file.write("\n".join(lines))


# ---------------------------------------------------------------------------------------------
# Moving the files, rewriting, sorting


def git_move(root, plan):
    """`git mv` of every file; folders left empty are removed."""
    for old, new in sorted(plan.moves.items()):
        os.makedirs(os.path.join(root, os.path.dirname(new)), exist_ok=True)
        subprocess.run(["git", "mv", old, new], cwd=root, check=True)
    for folder in sorted({os.path.dirname(old) for old in plan.moves}, key=len, reverse=True):
        full = os.path.join(root, folder)
        while os.path.isdir(full) and not os.listdir(full) and full != root:
            os.rmdir(full)
            full = os.path.dirname(full)


def run_fix_includes(root):
    """Runs the rewrite of scripts/fix-includes.py on the tree; the number of files it changed."""
    FIX.ROOT = root
    changes, _ = FIX.planned_changes(FIX.index_files())
    for path, new in changes:
        with open(os.path.join(root, path), "w", encoding="utf-8") as file:
            file.write(new)
    return len(changes)


def sort_lists(cmake_path, new_paths):
    """Sorts the QML_FILES of qt_add_qml_module and the file lines of qt_add_executable/qt_add_library
    when they hold a moved file (paths relative to the CMake file)."""
    with open(cmake_path, encoding="utf-8") as file:
        lines = file.read().split("\n")
    for start, body in CHECK.command_calls("\n".join(lines)):
        command = COMMAND.match(body[0])
        name = command.group(1).lower() if command else ""
        if name not in ("qt_add_qml_module", "qt_add_executable", "qt_add_library"):
            continue
        runs = file_runs(lines, start, len(body))
        for first, last in runs:
            run = lines[first:last]
            if any(token in new_paths for line in run for token in line.split()):
                key = (lambda line: line.split()[0]) if name == "qt_add_qml_module" else entry_key
                lines[first:last] = sorted(run, key=key)
    with open(cmake_path, "w", encoding="utf-8") as file:
        file.write("\n".join(lines))


def file_runs(lines, start, length):
    """(first, end) of each run of lines below a command's first line that hold only file names."""
    runs = []
    first = None
    for index in range(start + 1, start + length):
        tokens = CHECK.strip_comment(lines[index]).split()
        only_files = tokens and all(CHECK.FILE_TOKEN.fullmatch(token) for token in tokens)
        if only_files and first is None:
            first = index
        elif not only_files and first is not None:
            runs.append((first, index))
            first = None
    if first is not None:
        runs.append((first, start + length))
    return runs


# ---------------------------------------------------------------------------------------------
# Warnings


def tracked_files(root):
    output = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True, text=True, check=True).stdout
    return [path for path in output.split("\n") if path]


def read_text(root, path):
    try:
        with open(os.path.join(root, path), encoding="utf-8") as file:
            return file.read()
    except (UnicodeDecodeError, FileNotFoundError, IsADirectoryError):
        return None


def has_include_warnings(root, files, by_name, plan):
    """`__has_include("path")` with a path below src/ or tests/ that does not exist or that the run
    moves (it is not rewritten)."""
    warnings = []
    for path in files:
        if not path.endswith(CODE_SUFFIXES) or library(path) not in ("tests",) and not path.startswith("src/"):
            continue
        text = read_text(root, path) or ""
        for number, line in enumerate(text.split("\n"), 1):
            for match in HAS_INCLUDE.finditer(line):
                name = match.group(1)
                existing = [f"{r}/{name}" for r in FIX.INCLUDE_ROOTS if os.path.exists(os.path.join(root, r, name))]
                moved = next((plan.moves[p] for p in existing if p in plan.moves), None)
                if moved:
                    warnings.append(f"{path}:{number}: __has_include(\"{name}\") names a file the run moves to {moved}; "
                                    "not rewritten")
                if existing or "/" not in name:
                    continue
                now = by_name.get(os.path.basename(name))
                hint = f" (the file is now {now})" if now else ""
                warnings.append(f"{path}:{number}: __has_include(\"{name}\") names no file{hint}; not rewritten")
    return warnings


def folder_warnings(root, files, plan):
    """Folder paths (`${PROJECT_SOURCE_DIR}/src/...`, `${CMAKE_CURRENT_SOURCE_DIR}/...`) in CMake files
    and sources that name a folder the run moved files out of, or no folder at all."""
    left = {}
    for old in plan.moves:
        left.setdefault(os.path.dirname(old), []).append(os.path.basename(old))
    warnings = []
    for path in files:
        if not (path.endswith(("CMakeLists.txt", ".cmake")) or path.endswith(CODE_SUFFIXES)):
            continue
        text = read_text(root, path) or ""
        base = cmake_base(root, path)
        for number, line in enumerate(text.split("\n"), 1):
            for match in FOLDER_PATH.finditer(line):
                folder = os.path.normpath(match.group(2)) if match.group(1) == "PROJECT_SOURCE_DIR" else \
                    os.path.normpath(os.path.join(base, match.group(2)))
                if folder.endswith(SOURCE_SUFFIXES) or "." in os.path.basename(folder):
                    continue
                if folder in left:
                    names = ", ".join(sorted(left[folder])[:4]) + (" ..." if len(left[folder]) > 4 else "")
                    warnings.append(f"{path}:{number}: names the folder {folder}, which the run moved {names} out of; "
                                    f"not rewritten: {line.strip()[:120]}")
                elif not os.path.exists(os.path.join(root, folder)) and folder.startswith(("src/", "tests/")):
                    warnings.append(f"{path}:{number}: names the folder {folder}, which does not exist")
    return warnings


def cmake_base(root, path):
    """The folder ${CMAKE_CURRENT_SOURCE_DIR} stands for in a file: the nearest folder with a CMakeLists.txt."""
    folder = os.path.dirname(path)
    while folder and not os.path.exists(os.path.join(root, folder, "CMakeLists.txt")):
        folder = os.path.dirname(folder)
    return folder


def emptied_folders(root, plan, emptied_topics):
    """The folders the run leaves without files: every tracked file below them moves (a tests.cmake
    counts only when the run keeps it)."""
    files = tracked_files(root)
    result = []
    for folder in sorted({os.path.dirname(old) for old in plan.moves}):
        topic = folder.split("/")[1] if folder.startswith("tests/") and folder.count("/") == 1 else None
        remaining = [p for p in files if p.startswith(folder + "/") and p not in plan.moves
                     and not (topic in emptied_topics and p == f"{folder}/tests.cmake")]
        if not remaining:
            result.append(folder)
    return result


def leftover_warnings(root, files, plan, dry_run, emptied):
    """Old paths of moved files and of emptied folders left anywhere in the project (docs, scripts,
    comments): files as the full path and as the path below their include root, folders of src/ also
    below src/; the history files keep theirs. In a dry run, the lines the rewrite will change
    (includes, file paths in CMake code, translation locations) are left out."""
    needles = {}
    for old, new in plan.moves.items():
        needles[old] = new
        needles[old.split("/", 1)[1]] = new.split("/", 1)[1]
    for folder in emptied:
        needles[folder] = None
        if folder.startswith("src/"):
            needles[folder.split("/", 1)[1]] = None
    pattern = re.compile("|".join(re.escape(n) for n in sorted(needles, key=len, reverse=True)))
    warnings = []
    for path in files:
        if path.startswith(HISTORY) or not path.endswith(TEXT_SUFFIXES + (".ts",)) or path in plan.moves:
            continue
        text = read_text(root, path)
        if text is None or not pattern.search(text):
            continue
        for number, line in enumerate(text.split("\n"), 1):
            if HAS_INCLUDE.search(line):
                continue
            for match in pattern.finditer(line):
                target = needles[match.group(0)]
                if not boundary(line, match) or (dry_run and target and rewritten(path, line, match)):
                    continue
                if target:
                    warnings.append(f"{path}:{number}: still names {match.group(0)} (now {target})")
                else:
                    warnings.append(f"{path}:{number}: names the folder {match.group(0)}, which the run empties")
    return warnings


def rewritten(path, line, match):
    """True when the rewrite of fix-includes.py changes this mention of a moved file: an include, a
    path in CMake code (not in a comment), a location of a translation."""
    if path.endswith((".ts",)):
        return bool(FIX.TS_LOCATION.search(line))
    if path.endswith(("CMakeLists.txt", ".cmake")):
        comment = line.find("#")
        return comment < 0 or match.start() < comment
    return bool(FIX.INCLUDE.match(line)) and path.endswith(CODE_SUFFIXES)


def boundary(line, match):
    """True when the match is a whole path, not the tail of a longer one (`xsrc/a/QcA.h`, `b/a/QcA.h`,
    `a/QcA.hpp`); a bare file name (a header below tests/) counts only in quotes, as an include names it."""
    before = line[match.start() - 1] if match.start() else " "
    after = line[match.end()] if match.end() < len(line) else " "
    if "/" not in match.group(0):
        return before == '"'
    return not (before.isalnum() or before in "_-./") and not (after.isalnum() or after in "_-")


def checks(root):
    """The findings of scripts/check-cmake-lists.py and `scripts/fix-includes.py --check` on the tree."""
    findings = CHECK.check(root)
    FIX.ROOT = root
    changes, problems = FIX.planned_changes(FIX.index_files())
    problems += FIX.layer_violations(root, FIX.walk("src", CODE_SUFFIXES))
    findings += [f"fix-includes: {path}: includes or paths not written as decision E-418 asks" for path, _ in changes]
    findings += [f"fix-includes: {problem}" for problem in problems]
    return findings


# ---------------------------------------------------------------------------------------------
# The run


def dry_run_report(root, plan, tests, edits):
    """Prints what a run would do: the moves per folder, the CMake lines and tests that move and the
    number of files whose includes change."""
    folders = {}
    for old, new in sorted(plan.moves.items()):
        folders.setdefault((os.path.dirname(old), os.path.dirname(new)), []).append(os.path.basename(old))
    for (old, new), names in sorted(folders.items()):
        print(f"move {old} -> {new}: {len(names)} files ({', '.join(names[:6])}{' ...' if len(names) > 6 else ''})")
    for cmake, (_, count) in sorted(edits.items()):
        print(f"{os.path.relpath(cmake, root)}: {count} source lines into the blocks of their new folders")
    for line in tests.moved:
        print(f"test {line}")
    for topic in tests.created:
        print(f"new tests/{topic}/tests.cmake")
    for topic, (_, statements) in sorted(tests.files.items()):
        commands = [s for s in statements if not s.free]
        if topic in tests.created:
            continue
        if not commands:
            print(f"tests/{topic}/tests.cmake: emptied, removed with its include() line")
        elif not any(QC_ADD_TEST.search(s.code()) for s in commands):
            print(f"tests/{topic}/tests.cmake: keeps {len(commands)} commands but no test: "
                  + "; ".join(s.lines[0].strip() for s in commands))
    FIX.ROOT = root
    by_name = FIX.index_files()
    for old, new in plan.moves.items():
        by_name[os.path.basename(old)] = new if by_name.get(os.path.basename(old)) == old else None
    count = 0
    for path in [p for r in FIX.INCLUDE_ROOTS for p in FIX.walk(r, CODE_SUFFIXES)]:
        text = read_text(root, path)
        if FIX.rewrite_includes(path, text, by_name, []) != text:
            count += 1
    print(f"includes: {count} files would change (scripts/fix-includes.py)")


def parse_arguments(arguments):
    """(options, root, lists) of the command line, or a usage error as text."""
    options = set()
    root = os.path.dirname(SCRIPTS)
    lists = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument == "--root":
            if index + 1 >= len(arguments) or arguments[index + 1].startswith("--"):
                return "--root needs a folder"
            root = os.path.abspath(arguments[index + 1])
            index += 1
        elif argument.startswith("-"):
            if argument not in OPTIONS:
                return f"unknown option {argument}"
            options.add(argument)
        else:
            lists.append(argument)
        index += 1
    if not lists:
        return "no move list given"
    return options, root, lists


def main(arguments):
    parsed = parse_arguments(arguments)
    if isinstance(parsed, str):
        print(f"move-classes: {parsed}\n{__doc__}", file=sys.stderr)
        return 1
    options, root, lists = parsed
    if os.path.relpath(os.getcwd(), root).startswith(".."):
        print(f"move-classes: note: the current folder lies outside the project {root}; paths of the "
              "move lists are still relative to the project", file=sys.stderr)

    plan = Plan()
    for path in lists:
        plan.read(path)
    problems = validate(plan, root)
    if "--dry-run" not in options and "--validate" not in options:
        problems += dirty_tree(root)
    for problem in problems:
        print(f"move-classes: {problem}")
    if problems:
        print(f"move-classes: {len(problems)} problem(s); nothing moved", file=sys.stderr)
        return 1
    if "--validate" in options:
        print(f"move-classes: {len(plan.moves)} moves valid")
        return 0
    if "--dry-run" in options:
        return run(root, plan, dry_run=True)
    try:
        code = run(root, plan, dry_run=False)
    except Exception:  # noqa: BLE001 - any failure leaves a half-moved tree; say how to undo it
        print(f"move-classes: the run failed; {RECOVERY}", file=sys.stderr)
        raise
    if code == 1:
        print(f"move-classes: {RECOVERY}", file=sys.stderr)
    return code


def dirty_tree(root):
    """Problems when the tree has changes a run would mix with its own (tracked files anywhere,
    untracked ones below src/, tests/ and translations/)."""
    tracked = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root,
                             capture_output=True, text=True, check=True).stdout
    untracked = subprocess.run(["git", "status", "--porcelain", "--", "src", "tests", "translations"], cwd=root,
                               capture_output=True, text=True, check=True).stdout
    if tracked.strip() or untracked.strip():
        return ["the tree has changes (commit or set them aside first, git status lists them); a run must "
                "start clean so that `git reset --hard && git clean -fd` can undo it"]
    return []


def accepted(warnings, plan):
    """(warnings that count, accepted warnings): accept lines that match no warning are warnings themselves."""
    counting = []
    known = []
    used = set()
    for warning in warnings:
        match = next((text for text, _ in plan.accepts if text in warning), None)
        if match is None:
            counting.append(warning)
        else:
            known.append(warning)
            used.add(match)
    for text, where in plan.accepts:
        if text not in used:
            counting.append(f"{where}: accept line '{text}' matches no warning")
    return counting, known


def run(root, plan, dry_run):
    """Moves (or, in a dry run, reports) everything; the exit code (see the usage)."""
    tests = TestFiles(root, plan)
    tests.move_tests()
    block_problems = []
    edits = {cmake: move_block_lines(cmake, moves, block_problems) for cmake, moves in library_cmake_moves(plan, root).items()}
    problems = block_problems + tests.problems
    if dry_run:
        dry_run_report(root, plan, tests, edits)
    for problem in problems:
        print(f"move-classes: {problem}")
    if problems:
        print("move-classes: nothing moved", file=sys.stderr)
        return 1

    emptied = emptied_folders(root, plan, tests.emptied_topics())
    if not dry_run:
        move(root, plan, tests, edits)

    files = tracked_files(root)
    FIX.ROOT = root
    by_name = FIX.index_files()
    warnings = tests.warnings + has_include_warnings(root, files, by_name, plan) + folder_warnings(root, files, plan)
    warnings += leftover_warnings(root, files, plan, dry_run, emptied)
    counting, known = accepted(warnings, plan)
    for warning in known:
        print(f"move-classes: accepted: {warning}")
    for warning in counting:
        print(f"move-classes: warning: {warning}")
    findings = [] if dry_run else checks(root)
    for finding in findings:
        print(f"move-classes: check: {finding}")
    print(f"move-classes: {len(counting)} warning(s), {len(known)} accepted, {len(findings)} check finding(s)"
          + (" (dry run, nothing changed)" if dry_run else ""))
    return 1 if findings else 2 if counting else 0


def move(root, plan, tests, edits):
    """The changes of a real run: CMake blocks, tests.cmake files, git mv, includes, sorted lists."""
    for cmake, (text, _) in edits.items():
        with open(cmake, "w", encoding="utf-8") as file:
            file.write(text)
    emptied = tests.write()
    git_move(root, plan)
    changed = run_fix_includes(root)
    for cmake, moves in library_cmake_moves(plan, root).items():
        sort_lists(cmake, set(moves.values()))
    print(f"move-classes: {len(plan.moves)} files moved, {changed} files rewritten by fix-includes"
          + (f", removed tests.cmake of {', '.join(emptied)}" if emptied else ""))
    for line in tests.moved:
        print(f"move-classes: test {line}")
    new_files = [f"tests/{topic}/tests.cmake" for topic in tests.created if topic not in emptied]
    if new_files:
        subprocess.run(["git", "add", "--", *new_files], cwd=root, check=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
