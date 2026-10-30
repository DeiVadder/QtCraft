#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Finds repeated code in the C++ and QML sources of src/ (decision E-942), like CPD or jscpd on tokens.

The sources are split into tokens after blanking comments (scripts/qc_code_lines.py); preprocessor lines
(C++) and `import`/`pragma` lines (QML) are left out. Three kinds of findings:
  - clone: the same token sequence of at least --min-lines code lines (default 10) and --min-tokens tokens
    (default 60) in two places that do not overlap, literals (numbers, strings) counted as equal and
    identifiers kept, so a copy whose names differ is no clone;
  - repeat: a block of at least --repeat-lines code lines (default 5) written --repeat-count times or more
    (default 3) one after the other in the same file, with identifiers and literals counted as equal (the
    same structure); each block starts at the beginning of a line, overlapping runs keep the longest;
  - near-repeat (QML): --repeat-count or more objects of one type under one parent whose first 4 lines
    bind the same properties in the same order (the 16 `LazyScreen` blocks of Main.qml, whose values
    and later lines differ); left out when a repeat shows the same blocks.
Not reported: sequences with less than MIN_NAMED names among their tokens (tables of literals or
translation markers), repeated blocks that start with `struct`, `class` or `enum` (POD types written alike),
findings lying (NESTED of each place) inside a larger one, and short units (table rows, one-line cases).
A finding is skipped when `// NOLINT(qc-duplicate): <reason>` stands in one of its places or on the line
above one; a marker without a reason exempts nothing and is listed itself as `unreasoned`.
Heuristic on tokens, not a parser. Warning only: prints the findings and the counts; the exit code is 1 only
for a marker without a reason (2 for a bad option or a missing file).
Usage: scripts/check-duplicates.py [--list] [--changed] [--tests] [--min-lines N] [--min-tokens N]
                                   [--repeat-lines N] [--repeat-count N] [file ...]
  --list     prints every finding (largest first), otherwise only the counts per folder
  --changed  only findings with a place in a file changed against dev (committed, staged, unstaged, new)
  --tests    scans tests/ too (its findings are counted under tests/)
  file ...   only findings with a place in these files (implies --list)
  --self-test checks samples held in memory
"""

import argparse
import importlib.util
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_BRANCH = os.environ.get("QC_BASE_BRANCH", "dev")
NOLINT = "NOLINT(qc-duplicate):"
SUFFIXES = (".cpp", ".h", ".mm", ".qml", ".js")
MAX_GROUP = 40
# A finding whose tokens are less than this share names is data (tables, POD structs), not shared logic.
MIN_NAMED = 0.2
# A finding is nested in a larger one when this share of each of its places lies in one of the larger's.
NESTED = 0.8
# Near-repeats: QML objects whose first lines agree.
NEAR_PREFIX_LINES = 4
QML_OBJECT = re.compile(r"(?<![\w.])([A-Z]\w*(?:\.[A-Z]\w*)*)\s*\{")

TOKEN = re.compile(r"[A-Za-z_]\w*|\.?\d[\w.']*|\"x*\"|'x*'|`x*`|::|->|===|!==|==|!=|<=|>=|&&|\|\||\+\+|--"
                   r"|<<=|>>=|\+=|-=|\*=|/=|\S")
SKIPPED_LINE = {False: re.compile(r"^\s*#"), True: re.compile(r"^\s*(?:import|pragma)\b")}
KEYWORDS = {"if", "else", "for", "while", "do", "switch", "case", "default", "return", "break", "continue",
            "const", "constexpr", "static", "auto", "void", "bool", "int", "true", "false", "nullptr", "null",
            "this", "new", "delete", "class", "struct", "enum", "public", "private", "protected", "function",
            "property", "readonly", "required", "signal", "var", "let", "id", "undefined", "override", "final",
            "virtual", "template", "typename", "using", "namespace", "operator", "inline", "explicit"}


def load_code_lines():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qc_code_lines.py")
    spec = importlib.util.spec_from_file_location("qc_code_lines", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


QC_CODE_LINES = load_code_lines()


class Tokens:
    """The tokens of all scanned files in one list: exact kind (literals equal) and structure kind
    (identifiers equal too), with file and line of each; a file ends with a token of its own."""

    def __init__(self):
        self.exact = []
        self.shape = []
        self.where = []
        self.named = []
        self.ids = {}

    def number(self, text):
        return self.ids.setdefault(text, len(self.ids))

    def add_file(self, path, text):
        qml = QC_CODE_LINES.is_qml(path)
        masked = QC_CODE_LINES.masked(text, path)
        for line_number, line in enumerate(masked.split("\n"), start=1):
            if SKIPPED_LINE[qml].match(line):
                continue
            for match in TOKEN.finditer(line):
                token = match.group()
                literal = token[0] in "\"'`" or token[0].isdigit() or (token[0] == "." and len(token) > 1)
                identifier = not literal and (token[0].isalpha() or token[0] == "_") and token not in KEYWORDS
                exact = "LIT" if literal else token
                self.exact.append(self.number(exact))
                self.shape.append(self.number("ID" if identifier else exact))
                self.where.append((path, line_number))
                self.named.append(identifier)
        self.exact.append(self.number("\0" + path))
        self.shape.append(self.exact[-1])
        self.where.append((path, 0))
        self.named.append(False)


def window_groups(sequence, width):
    """Start positions grouped by equal windows of `width` tokens (rolling hash, groups of two or more)."""
    modulus, base = (1 << 61) - 1, 1_000_003
    power = pow(base, width - 1, modulus)
    groups, value = {}, 0
    for index, token in enumerate(sequence):
        value = (value * base + token + 1) % modulus
        if index >= width - 1:
            groups.setdefault(value, []).append(index - width + 1)
            value = (value - (sequence[index - width + 1] + 1) * power) % modulus
    return [group for group in groups.values() if len(group) > 1]


def extent(sequence, first, second):
    """The number of equal tokens from `first` and `second` on."""
    length = 0
    while second + length < len(sequence) and sequence[first + length] == sequence[second + length]:
        length += 1
    return length


def code_lines(tokens, start, length):
    """The distinct lines of the tokens [start, start + length)."""
    return len({tokens.where[index][1] for index in range(start, start + length)})


def place(tokens, start, length):
    path = tokens.where[start][0]
    return (path, tokens.where[start][1], tokens.where[start + length - 1][1])


def mostly_data(tokens, start, length):
    """True when fewer than MIN_NAMED of the tokens are names: tables of literals, lists of translation
    markers, POD structs written alike (data, not logic to share)."""
    return sum(tokens.named[start:start + length]) < MIN_NAMED * length


def line_start(tokens, start, limit):
    """The first token at or after start (at most `limit` tokens on) that begins a line, or start."""
    for index in range(start, start + limit):
        if index == 0 or tokens.where[index][1] != tokens.where[index - 1][1]:
            return index
    return start


def find_clones(tokens, min_tokens, min_lines):
    """Maximal pairs of equal exact sequences that do not overlap, as (lines, places)."""
    sequence, found = tokens.exact, {}
    for group in window_groups(sequence, min_tokens):
        group = group[:MAX_GROUP]
        for first, second in zip(group, group[1:]):
            if first > 0 and sequence[first - 1] == sequence[second - 1]:
                continue
            length = extent(sequence, first, second)
            same_file = tokens.where[first][0] == tokens.where[second][0]
            if same_file and first + length > second:
                continue
            lines = code_lines(tokens, first, length)
            if lines < min_lines or mostly_data(tokens, first, length):
                continue
            key = tuple(sequence[first:first + length])
            entry = found.setdefault(key, (lines, set()))
            entry[1].update({place(tokens, first, length), place(tokens, second, length)})
    return [("clone", lines, sorted(places)) for lines, places in found.values()]


def find_repeats(tokens, min_lines, min_count):
    """Runs of one block written `min_count` times or more back to back (equal structure), as
    (lines of one block, places of the blocks). Each block starts at the beginning of a line; runs that
    overlap keep the longest."""
    sequence, runs = tokens.shape, []
    # Types written alike one after the other (POD structs, enums) are declarations, not logic to share.
    declarations = {tokens.number(word) for word in ("struct", "class", "enum")}
    for group in window_groups(sequence, 24):
        group = group[:MAX_GROUP]
        for first, second in zip(group, group[1:]):
            period = second - first
            if tokens.where[first][0] != tokens.where[second][0]:
                continue
            if first > 0 and sequence[first - 1] == sequence[second - 1]:
                continue
            length = extent(sequence, first, second)
            start = line_start(tokens, first, min(period, length))
            length -= start - first
            count = (length + period) // period
            if length < period or count < min_count or code_lines(tokens, start, period) < min_lines:
                continue
            if mostly_data(tokens, start, period) or tokens.exact[start] in declarations:
                continue
            runs.append((start, period, count))
    found, taken = [], []
    for start, period, count in sorted(runs, key=lambda run: (-run[1] * run[2], run[0])):
        end = start + period * count
        if any(start < other_end and other_start < end for other_start, other_end in taken):
            continue
        taken.append((start, end))
        places = [place(tokens, start + index * period, period) for index in range(count)]
        found.append(("repeat", code_lines(tokens, start, period), places))
    return found


def qml_objects(masked):
    """The QML objects of a masked file as (type, open brace, close brace, open brace of the parent)."""
    pairs, stack = {}, []
    for index, character in enumerate(masked):
        if character == "{":
            stack.append(index)
        elif character == "}" and stack:
            pairs[stack.pop()] = index
    objects = []
    for match in QML_OBJECT.finditer(masked):
        brace = match.end() - 1
        if brace in pairs:
            outer = [start for start, end in pairs.items() if start < brace and pairs[brace] < end]
            objects.append((match.group(1), brace, pairs[brace], max(outer) if outer else -1))
    return objects


def body_lines(masked, brace, close):
    """The code lines directly in the body of the object at brace: (line number, key) for each line that
    starts at the body's own depth, the key being the property name with the shape of its value."""
    lines, depth, number = [], 0, masked.count("\n", 0, brace) + 1
    text = masked[brace + 1:close]
    for line in text.split("\n"):
        if depth == 0 and line.strip() and line.strip() != "}":
            match = re.match(r"\s*([\w.]+)\s*:(?!:)(.*)", line)
            value = match.group(2) if match else line
            shape = " ".join("I" if token[0].isalpha() or token[0] == "_" else token
                             for token in TOKEN.findall(value))
            lines.append((number, (match.group(1) if match else "", shape)))
        depth += line.count("{") - line.count("}")
        number += 1
    return lines[1:] if lines and lines[0][1] == ("", "") else lines


def find_near_repeats(sources, prefix_lines, min_count):
    """QML objects of one type under one parent, `min_count` or more, whose first `prefix_lines` lines
    bind the same properties in the same order (`LazyScreen` blocks that differ in their values and
    further down)."""
    found = []
    for path in sorted(sources):
        if not path.endswith(".qml"):
            continue
        masked = QC_CODE_LINES.masked(sources[path], path)
        groups = {}
        for kind, brace, close, parent in qml_objects(masked):
            lines = body_lines(masked, brace, close)
            if len(lines) < prefix_lines:
                continue
            names = tuple(entry[1][0] for entry in lines[:prefix_lines])
            if not all(names):
                continue
            key = (kind, parent, names)
            groups.setdefault(key, []).append((path, masked.count("\n", 0, brace) + 1,
                                               masked.count("\n", 0, close) + 1))
        for places in groups.values():
            if len(places) >= min_count:
                found.append(("near-repeat", prefix_lines, sorted(places)))
    return found


def overlap(inner, outer):
    """The share of inner's lines that lie in outer."""
    if inner[0] != outer[0]:
        return 0.0
    common = min(inner[2], outer[2]) - max(inner[1], outer[1]) + 1
    return max(0, common) / (inner[2] - inner[1] + 1)


def span(finding):
    """The lines from the first to the last place of a near-repeat (its places lie in one file)."""
    places = finding[2]
    return (places[0][0], places[0][1], places[-1][2])


def without_nested(findings):
    """Findings in order of size, without those whose places all lie (to NESTED or more) inside places of a
    larger one; a near-repeat covers the whole stretch of its objects."""
    order = {"near-repeat": 0, "repeat": 1, "clone": 2}
    blocks = [place for other in findings if other[0] == "repeat" for place in other[2]]
    # A near-repeat that a back-to-back repeat already shows whole adds nothing.
    findings = [finding for finding in findings if finding[0] != "near-repeat"
                or not all(any(overlap(inner, block) >= NESTED for block in blocks) for inner in finding[2])]
    findings = sorted(findings, key=lambda item: (order[item[0]], -item[1] * len(item[2]), item[2]))
    kept = []
    for finding in findings:
        outer = [span(other) if other[0] == "near-repeat" else place for other in kept for place in other[2]]
        if all(any(overlap(inner, wide) >= NESTED for wide in outer) for inner in finding[2]):
            continue
        kept.append(finding)
    return kept


def markers(sources):
    """Lines with the opt-out marker per file: (path, line) -> True when the marker gives a reason."""
    found = {}
    for path, text in sources.items():
        for number, line in enumerate(text.split("\n"), start=1):
            position = line.find(NOLINT)
            if position >= 0:
                found[(path, number)] = bool(line[position + len(NOLINT):].strip())
    return found


def exempt(finding, marks):
    """True when a marker with a reason stands in one of the places or on the line above it."""
    return any((path, line) in marks for path, first, last in finding[2] for line in range(first - 1, last + 1))


def analyse(sources, min_lines=10, min_tokens=60, repeat_lines=5, repeat_count=3):
    """All findings of the given sources (path -> text): (kind, lines, places)."""
    tokens = Tokens()
    for path in sorted(sources):
        tokens.add_file(path, sources[path])
    findings = without_nested(find_clones(tokens, min_tokens, min_lines)
                              + find_repeats(tokens, repeat_lines, repeat_count)
                              + find_near_repeats(sources, NEAR_PREFIX_LINES, repeat_count))
    marks = markers(sources)
    reasoned = {key for key, reason in marks.items() if reason}
    kept = [finding for finding in findings if not exempt(finding, reasoned)]
    kept += [("unreasoned", 0, [(path, line, line)]) for (path, line), reason in sorted(marks.items())
             if not reason]
    return kept


def source_files(with_tests):
    folders = ["src/"] + (["tests/"] if with_tests else [])
    output = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard", *folders], cwd=ROOT,
                            capture_output=True, text=True, check=False).stdout
    return sorted({line for line in output.split("\n") if line.endswith(SUFFIXES)})


def changed_files():
    """Files below src/ and tests/ changed against the base branch: committed, staged, unstaged and new."""
    commands = (["git", "diff", "--name-only", f"{BASE_BRANCH}...HEAD"], ["git", "diff", "--name-only", "HEAD"],
                ["git", "ls-files", "--others", "--exclude-standard"])
    files = set()
    for command in commands:
        output = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False).stdout
        files.update(line for line in output.split("\n") if line.startswith(("src/", "tests/")))
    return files


def message(finding):
    kind, lines, places = finding
    if kind == "unreasoned":
        return f"{places[0][0]}:{places[0][1]}: {NOLINT} without a reason"
    where = ", ".join(f"{path}:{first}-{last}" for path, first, last in places)
    if kind == "near-repeat":
        return (f"{places[0][0]}:{places[0][1]}: {len(places)} objects of one type start with the same "
                f"{lines} lines ({where}); a component with the differences as properties")
    if kind == "repeat":
        advice = "a component or a Repeater" if QC_CODE_LINES.is_qml(places[0][0]) else "a function or a table"
        return (f"{places[0][0]}:{places[0][1]}: block of {lines} lines repeated {len(places)} times back to back "
                f"({where}); {advice}")
    return f"{places[0][0]}:{places[0][1]}: {lines} lines in {len(places)} places ({where}); share them"


def folder(path):
    parts = path.split("/")
    return "/".join(parts[:2]) + "/" if len(parts) > 2 else parts[0] + "/"


def counts(findings):
    per_folder = {}
    for kind, lines, places in findings:
        key = (folder(places[0][0]), kind)
        total = per_folder.setdefault(key, [0, 0])
        total[0] += 1
        total[1] += lines * (len(places) - 1)
    return per_folder


def parse(arguments):
    parser = argparse.ArgumentParser(prog="check-duplicates.py", description="Repeated code (E-942).")
    parser.add_argument("--list", action="store_true", help="print every finding")
    parser.add_argument("--changed", action="store_true", help="only findings in files changed against dev")
    parser.add_argument("--tests", action="store_true", help="scan tests/ too")
    parser.add_argument("--self-test", action="store_true", help="check samples held in memory")
    parser.add_argument("--min-lines", type=int, default=10, help="code lines of a clone (default 10)")
    parser.add_argument("--min-tokens", type=int, default=60, help="tokens of a clone (default 60)")
    parser.add_argument("--repeat-lines", type=int, default=5, help="code lines of a repeated block (default 5)")
    parser.add_argument("--repeat-count", type=int, default=3, help="repetitions of a block (default 3)")
    parser.add_argument("files", nargs="*", help="only findings with a place in these files")
    options = parser.parse_args(arguments)
    missing = [path for path in options.files if not os.path.isfile(path)]
    if missing:
        parser.error("no such file: " + ", ".join(missing))
    return options


def main(arguments):
    options = parse(arguments)
    if options.self_test:
        return self_test()
    files = {os.path.relpath(os.path.abspath(path), ROOT) for path in options.files}
    selected = files or (changed_files() if options.changed else None)
    sources = {}
    for path in source_files(options.tests or any(path.startswith("tests/") for path in files)):
        sources[path] = QC_CODE_LINES.read_source(os.path.join(ROOT, path))
    findings = analyse(sources, options.min_lines, options.min_tokens, options.repeat_lines, options.repeat_count)
    if selected is not None:
        findings = [finding for finding in findings if any(path in selected for path, _, _ in finding[2])]
    if options.list or selected is not None:
        for finding in findings:
            print(message(finding))
    for (where, kind), (number, lines) in sorted(counts(findings).items()):
        print(f"{where}: {number} {kind}s, {lines} repeated lines")
    number = {kind: sum(1 for finding in findings if finding[0] == kind)
              for kind in ("clone", "repeat", "near-repeat", "unreasoned")}
    print(f"duplicates (warning only, E-942): {number['clone']} clones, {number['repeat']} repeated blocks, "
          f"{number['near-repeat']} near-repeats in {len(sources)} files, "
          f"{number['unreasoned']} {NOLINT} marker(s) without a reason")
    return 1 if number["unreasoned"] else 0


CLONE_A = """// SPDX
#include "QcA.h"
int QcA::sum(const QList<int> &values) const
{
    int total = 0;
    for (const int value : values) {
        if (value > 3) {
            total += value * 2;
        } else {
            total -= value;
        }
        if (total > m_limit) {
            total = m_limit;
        }
    }
    qCDebug(lcA) << "sum" << total << values.size();
    return total + m_offset;
}
"""

REPEAT_QML = """import QtQuick
Item {
""" + "".join(f"""    LazyScreen {{
        anchors.fill: parent
        keepAliveMs: root.keepAlive
        shown: game.overlay === GameController.Overlay.{name}
        screen: {name}Screen {{
            game: root.controller
        }}
    }}
""" for name in ("Chest", "Furnace", "Trade", "Anvil")) + "}\n"

NEAR_QML = """import QtQuick
Item {
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.keepAlive
        logCreation: root.log
        shown: game.overlay === GameController.Overlay.Chest
        preload: game.creative
        screen: ChestScreen {}
    }
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.keepAlive
        logCreation: root.log
        shown: [GameController.Overlay.Pause, GameController.Overlay.Settings].includes(game.overlay)
        screen: PauseMenu { game: root.controller }
    }
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.keepAlive
        logCreation: root.log
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Trade
        screen: TradeScreen {
            game: root.controller
            onClosed: game.closeScreen()
        }
    }
}
"""

NAMES_A = "constexpr std::array<Name, 14> ruleNames{{\n" + "".join(
    f'    {{"key{i}", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Name {i}")}},\n' for i in range(14)) + "}};\n"

TABLE = "static constexpr Row rows[] = {\n" + "".join(f"    {{\"name{i}\", {i}, {i * 2}}},\n" for i in range(30)) + "};\n"


def self_test():
    failures = []
    renamed = CLONE_A.replace("QcA", "QcB").replace("lcA", "lcB")
    other = CLONE_A.replace("value", "item").replace("QcA", "QcC")
    found = analyse({"src/a/QcA.cpp": CLONE_A, "src/b/QcB.cpp": renamed.replace("3", "7"), "src/c/QcC.cpp": other})
    clones = [finding for finding in found if finding[0] == "clone"]
    if len(clones) != 1 or [place[0] for place in clones[0][2]] != ["src/a/QcA.cpp", "src/b/QcB.cpp"]:
        failures.append(f"one clone of QcA in QcB (literals differ, names partly), none in QcC: {found}")

    found = analyse({"src/app/qml/Main.qml": REPEAT_QML})
    if [(kind, lines, len(places)) for kind, lines, places in found] != [("repeat", 8, 4)]:
        failures.append(f"four LazyScreen blocks of 8 lines: {found}")
    found = analyse({"src/app/qml/Main.qml": REPEAT_QML.replace("    LazyScreen {", "    // NOLINT(qc-duplicate): sample\n"
                                                                "    LazyScreen {", 1)})
    if found:
        failures.append(f"marked repeat still listed: {found}")
    found = analyse({"src/a/QcTable.cpp": TABLE})
    if found:
        failures.append(f"table rows are no finding: {found}")
    unreasoned = CLONE_A.replace("values) const\n", "values) const // NOLINT(qc-duplicate):\n")
    found = analyse({"src/a/QcA.cpp": unreasoned, "src/b/QcB.cpp": renamed})
    if [(finding[0], finding[2][0][1]) for finding in found] != [("clone", 3), ("unreasoned", 3)]:
        failures.append(f"marker without reason on the clone's first line exempts nothing and is listed: {found}")
    inside = CLONE_A.replace("total -= value;", "total -= value; // NOLINT(qc-duplicate): sample")
    found = analyse({"src/a/QcA.cpp": inside, "src/b/QcB.cpp": renamed})
    if found:
        failures.append(f"marker with a reason inside the range exempts the clone: {found}")
    found = analyse({"src/app/qml/Main.qml": NEAR_QML})
    if [(kind, len(places)) for kind, _, places in found] != [("near-repeat", 3)]:
        failures.append(f"three LazyScreen blocks with the same first lines: {found}")
    found = analyse({"src/a/QcNames.cpp": NAMES_A, "src/b/QcNames.cpp": NAMES_A.replace("ruleNames", "modeNames")})
    if found:
        failures.append(f"tables of translation markers are no clone: {found}")
    for failure in failures:
        print(f"FAIL: {failure}")
    print("check-duplicates self-test: " + ("failed" if failures else "all samples judged correctly"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
