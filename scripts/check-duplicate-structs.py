#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Finds structs in src/ whose data members repeat those of another struct (decision E-975).

Each named `struct` of the C++ sources (headers and sources, nested ones included) is read for its data
members: declarations directly in its body, without functions, nested types, static members, aliases and
macros (`Q_GADGET`, `Q_PROPERTY(...)`). A member is its type (spaces normalised) and its name; several
declarators share the type (`int x, y, z;`), initialisers are dropped. Two structs with at least
MIN_FIELDS members whose members agree in name and type (order ignored) are one finding: the later ones
should use the earlier type (`QcBlockPos` instead of a cell of three `int`, `QcXzRect` instead of an own
rectangle). Same types under other names (`Quantile` and `Point` of two `double`) are no finding;
struct templates (`template<...> struct`) are left out, their members depend on the parameters.
A struct is left out when `// NOLINT(qc-duplicate-struct): <reason>` stands on its line or the line above;
a marker without a reason exempts nothing and is listed itself as `unreasoned`.
Heuristic on the masked text (scripts/qc_code_lines.py), not a parser. Prints the groups and the count; the
exit code is 1 for a marker without a reason and when the count over src/ rises above its baseline (ratchet,
scripts/qc_ratchet.py and scripts/check-baseline.json), 2 for a bad option or a missing file.
Usage: scripts/check-duplicate-structs.py [--list] [--changed] [file ...]
  --list     prints every group, otherwise only the count
  --changed  only groups with a struct in a file changed against dev (committed, staged, unstaged, new)
  file ...   only groups with a struct in these files (implies --list)
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
NOLINT = "NOLINT(qc-duplicate-struct):"
SUFFIXES = (".cpp", ".h", ".mm")
MIN_FIELDS = 2
STRUCT = re.compile(r"\bstruct\s+(?:alignas\s*\([^)]*\)\s*)?([A-Za-z_]\w*)\s*(?:final\s*)?(?::[^;{()]*)?\{")
MACRO = re.compile(r"^\s*[A-Z][A-Z0-9_]{2,}\b\s*")
SKIPPED_START = re.compile(r"^\s*(?:using|typedef|friend|static|enum|struct|class|union|template|"
                           r"public|private|protected|operator|explicit|virtual|constexpr\s+static)\b")
TEMPLATE_BEFORE = re.compile(r"\btemplate\s*<[^;{}]*>\s*$")
DECLARATOR = re.compile(r"^(.*?)([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$", re.DOTALL)


def load_module(name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), f"{name}.py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


QC_CODE_LINES = load_module("qc_code_lines")
QC_RATCHET = load_module("qc_ratchet")


def matching(text, start, opening, closing):
    """Index after the bracket that closes the one at `start`, or len(text)."""
    depth = 0
    for index in range(start, len(text)):
        if text[index] == opening:
            depth += 1
        elif text[index] == closing:
            depth -= 1
            if depth == 0:
                return index + 1
    return len(text)


def without_macros(statement):
    """The statement without leading macro calls (`Q_GADGET`, `Q_PROPERTY(...)`, `QML_ELEMENT`)."""
    while True:
        found = MACRO.match(statement)
        if not found:
            return statement
        rest = statement[found.end():]
        if rest.startswith("("):
            rest = rest[matching(rest, 0, "(", ")"):]
        elif rest[:1].isalnum() or rest[:1] == "_":
            # A macro directly followed by a name is a type (`QVector3D` does not match, `GLint x` would).
            return statement
        statement = rest


def top_level_split(text, separator):
    """Parts of `text` split at `separator` outside (), <>, {} and []."""
    parts, depth, current = [], 0, []
    for char in text:
        if char in "(<{[":
            depth += 1
        elif char in ")>}]":
            depth -= 1
        if char == separator and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    parts.append("".join(current))
    return parts


def strip_template_arguments(text):
    """`text` without the contents of <...> (so `std::function<void(int)>` shows no parenthesis)."""
    result, depth = [], 0
    for char in text:
        if char == "<":
            depth += 1
        elif char == ">" and depth:
            depth -= 1
        elif depth == 0:
            result.append(char)
    return "".join(result)


def normalised(type_text):
    text = re.sub(r"\s+", " ", type_text).strip()
    return re.sub(r"\s*([<>,*&:])\s*", r"\1", text)


def fields_of(statement):
    """The members (type, name) a body statement declares, or [] when it is no data member."""
    statement = without_macros(statement).strip()
    if not statement or SKIPPED_START.match(statement) or statement.endswith(":"):
        return []
    head = re.split(r"[={]", strip_template_arguments(statement), maxsplit=1)[0]
    if "(" in head or "operator" in head.split():
        return []
    fields, kind = [], None
    for part in top_level_split(statement, ","):
        part = re.split(r"\s*(?:=|\{)", part, maxsplit=1)[0]
        part = re.sub(r"\s*:\s*\d+\s*$", "", part)  # bit field width
        found = DECLARATOR.match(part.strip())
        if not found:
            return []
        prefix = normalised(re.sub(r"\[\[.*?\]\]|\bmutable\b", "", found.group(1)))
        if kind is None:
            kind = prefix.rstrip("*&")
            if not kind:
                return []
            prefix = prefix[len(kind):]
        # Each declarator has the base type and its own `*` or `&` (`int *a, b;`: `int*` and `int`).
        fields.append((kind + prefix + (found.group(3) or ""), found.group(2)))
    return fields


def body_statements(body):
    """Statements at the top level of a struct body; nested blocks of functions and types are skipped."""
    statements, current, index = [], [], 0
    while index < len(body):
        char = body[index]
        if char == "{":
            end = matching(body, index, "{", "}")
            text = "".join(current)
            head = strip_template_arguments(without_macros(text))
            # A parenthesis before any `=` makes a function body; after it, a lambda initialiser.
            function = "(" in head.split("=", 1)[0]
            if function or (SKIPPED_START.match(head) and "=" not in head) or not text.strip():
                current = [] if function else current + ["{}"]
                index = end
                continue
            current.append(body[index:end])
            index = end
            continue
        if char == ";":
            statements.append("".join(current))
            current = []
        elif char == ":" and re.search(r"\b(?:public|private|protected)\s*$", "".join(current)):
            current = []
        else:
            current.append(char)
        index += 1
    return statements


def structs_of(path, text):
    """(name, path, line, fields) for each named struct definition of the masked `text`."""
    masked = QC_CODE_LINES.masked(text, path)
    masked = re.sub(r"^[ \t]*#.*$", "", masked, flags=re.MULTILINE)
    found = []
    for match in STRUCT.finditer(masked):
        if TEMPLATE_BEFORE.search(masked, max(0, match.start() - 400), match.start()):
            continue
        brace = match.end() - 1
        body = masked[brace + 1:matching(masked, brace, "{", "}") - 1]
        fields = []
        for statement in body_statements(body):
            fields += fields_of(statement)
        line = masked.count("\n", 0, match.start()) + 1
        found.append((match.group(1), path, line, tuple(sorted(fields))))
    return found


def markers(sources):
    found = {}
    for path, text in sources.items():
        for number, line in enumerate(text.split("\n"), start=1):
            position = line.find(NOLINT)
            if position >= 0:
                found[(path, number)] = bool(line[position + len(NOLINT):].strip())
    return found


def analyse(sources):
    """Groups of structs with equal members, as lists of (name, path, line, fields), plus unreasoned markers."""
    marks = markers(sources)
    by_fields = {}
    for path in sorted(sources):
        for struct in structs_of(path, sources[path]):
            name, where, line, fields = struct
            if len(fields) < MIN_FIELDS or marks.get((where, line)) or marks.get((where, line - 1)):
                continue
            by_fields.setdefault(fields, []).append(struct)
    groups = [group for group in by_fields.values() if len(group) > 1]
    groups.sort(key=lambda group: (group[0][1], group[0][2]))
    unreasoned = [(path, line) for (path, line), reason in sorted(marks.items()) if not reason]
    return groups, unreasoned


def source_files():
    output = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard", "src/"], cwd=ROOT,
                            capture_output=True, text=True, check=False).stdout
    return sorted({line for line in output.split("\n") if line.endswith(SUFFIXES)})


def changed_files():
    """Files below src/ changed against the base branch: committed, staged, unstaged and new."""
    commands = (["git", "diff", "--name-only", f"{BASE_BRANCH}...HEAD"], ["git", "diff", "--name-only", "HEAD"],
                ["git", "ls-files", "--others", "--exclude-standard"])
    files = set()
    for command in commands:
        output = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False).stdout
        files.update(line for line in output.split("\n") if line.startswith("src/"))
    return files


def message(group):
    first = group[0]
    members = ", ".join(f"{kind} {name}" for kind, name in first[3])
    where = ", ".join(f"{name} {path}:{line}" for name, path, line, _ in group[1:])
    return (f"{first[1]}:{first[2]}: struct {first[0]} ({members}) has the same members as {where}; "
            "share one type")


def parse(arguments):
    parser = argparse.ArgumentParser(prog="check-duplicate-structs.py", description="Repeated structs (E-975).")
    parser.add_argument("--list", action="store_true", help="print every group")
    parser.add_argument("--changed", action="store_true", help="only groups in files changed against dev")
    parser.add_argument("--self-test", action="store_true", help="check samples held in memory")
    parser.add_argument("files", nargs="*", help="only groups with a struct in these files")
    options = parser.parse_args(arguments)
    missing = [path for path in options.files if not os.path.isfile(path)]
    if missing:
        parser.error("no such file: " + ", ".join(missing))
    return options


def duplicates(groups):
    """Structs that repeat an earlier one: the ratcheted count."""
    return sum(len(group) - 1 for group in groups)


def main(arguments):
    options = parse(arguments)
    if options.self_test:
        return self_test()
    files = {os.path.relpath(os.path.abspath(path), ROOT) for path in options.files}
    selected = files or (changed_files() if options.changed else None)
    sources = {path: QC_CODE_LINES.read_source(os.path.join(ROOT, path)) for path in source_files()}
    groups, unreasoned = analyse(sources)
    risen = QC_RATCHET.check("check-duplicate-structs.py", {"struct": duplicates(groups)})
    if selected is not None:
        groups = [group for group in groups if any(struct[1] in selected for struct in group)]
    if options.list or selected is not None:
        for group in groups:
            print(message(group))
    for path, line in unreasoned:
        print(f"{path}:{line}: {NOLINT} without a reason")
    print(f"duplicate structs (ratchet, E-975): {duplicates(groups)} structs repeat another in {len(groups)} "
          f"groups over {len(sources)} files, {len(unreasoned)} {NOLINT} marker(s) without a reason")
    return 1 if unreasoned or risen else 0


SAMPLE_A = """// SPDX
struct QcXzRect
{
    int minX = 0;
    int minZ = 0;
    int maxX = 0;
    int maxZ = 0;
    [[nodiscard]] bool contains(int x, int z) const { return x >= minX && x <= maxX; }
};
"""

SAMPLE_B = """class QcVillage
{
    struct Area {
        Q_GADGET
        Q_PROPERTY(int minX MEMBER minX)
    public:
        int minX{0}, minZ{0};
        int maxX = 3;
        int maxZ;
        static int count;
        enum class Kind { A, B };
        void grow(int by) { maxX += by; }
    };
    struct Quantile { double share = 0.0; double value = 0.0; };
    struct Point { double x; double y; };
    struct Single { int minX; };
    struct Callback { std::function<void(int, int)> done; QList<int> ids; };
    struct Pointers { int *first, second; [[maybe_unused]] const QString *&third; };
    struct Lambda { std::function<int(int)> twice = [](int a) { return 2 * a; }; int calls = 0; };
};

template<typename T>
struct QcTemplateRect
{
    int minX = 0;
    int minZ = 0;
    int maxX = 0;
    int maxZ = 0;
};
"""


def self_test():
    failures = []
    groups, unreasoned = analyse({"src/a/QcXzRect.h": SAMPLE_A, "src/b/QcVillage.h": SAMPLE_B})
    names = [[struct[0] for struct in group] for group in groups]
    if names != [["QcXzRect", "Area"]] or unreasoned:
        failures.append(f"Area repeats QcXzRect (macros, access, initialisers, functions skipped): {names}")
    fields = dict((struct[0], struct[3]) for struct in structs_of("src/b/QcVillage.h", SAMPLE_B))
    if fields.get("Callback") != (("QList<int>", "ids"), ("std::function<void(int,int)>", "done")):
        failures.append(f"members with template arguments and parentheses: {fields.get('Callback')}")
    if fields.get("Pointers") != (("const QString*&", "third"), ("int", "second"), ("int*", "first")):
        failures.append(f"each declarator keeps its own pointer, attributes dropped: {fields.get('Pointers')}")
    if fields.get("Lambda") != (("int", "calls"), ("std::function<int(int)>", "twice")):
        failures.append(f"a member with a lambda initialiser is a member: {fields.get('Lambda')}")
    if "QcTemplateRect" in fields:
        failures.append("struct templates are left out")
    if fields.get("Quantile") == fields.get("Point"):
        failures.append("same types under other names are no finding")
    marked = SAMPLE_B.replace("    struct Area {", "    // NOLINT(qc-duplicate-struct): sample\n    struct Area {")
    groups, unreasoned = analyse({"src/a/QcXzRect.h": SAMPLE_A, "src/b/QcVillage.h": marked})
    if groups or unreasoned:
        failures.append(f"a marker with a reason exempts the struct: {groups}")
    bare = SAMPLE_B.replace("    struct Area {", "    // NOLINT(qc-duplicate-struct):\n    struct Area {")
    groups, unreasoned = analyse({"src/a/QcXzRect.h": SAMPLE_A, "src/b/QcVillage.h": bare})
    if len(groups) != 1 or unreasoned != [("src/b/QcVillage.h", 3)]:
        failures.append(f"a marker without a reason exempts nothing and is listed: {groups} {unreasoned}")
    for failure in failures:
        print(f"FAIL: {failure}")
    print("check-duplicate-structs self-test: " + ("failed" if failures else "all samples judged correctly"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
