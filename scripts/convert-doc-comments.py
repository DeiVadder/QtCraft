#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Turns the `//` comments above the API of headers and QML files into QDoc comments (E-939).

The comment block directly above a public or protected declaration of a header (class, struct, union,
namespace, enum and its enumerators, function, signal, variable, type alias, Q_PROPERTY line) and
above the root object of a QML file or one of its properties, signals and functions becomes a
`/*! ... */` block with the same text in the form of scripts/qc_doc_format.py, no topic command
(scripts/api-docs.py adds it from the declaration): types get `\\brief` with their first sentence and
a paragraph with the rest; `name` in backticks becomes `\\a name` for a parameter of the function
below and `\\c {name}` otherwise; `- item` lists become \\list; a backslash becomes `\\\\`; a
function with a result whose comment starts with "The", "Whether" or "True if/when" gets "Returns".
A member comment keeps its number of lines where it fits in 100 characters. QDoc comments with a line
longer than 100 characters are wrapped anew (not those with \\code, \\table or a topic command).
Left alone: private members (and everything in a private nested type), friend functions (not
scanned), comments with a blank line before the declaration, the licence header, marker lines
(`// NOLINT...`, `// clang-format`), which stay directly above the declaration, comments after code on
the same line (except that the docs read those of enumerators), comments inside function bodies and
nested QML objects, and other `/* */` comments. Running it again changes nothing. A file whose code
(everything outside comments, whitespace ignored) would change is left alone and named (exit code 1).
Usage: scripts/convert-doc-comments.py [--dry-run | --check [--skip-internal] | --params [--apply]]
                                       [--self-test] <file or folder> ...
  (no option)  converts the files in place and names them
  --dry-run    prints the changes as a unified diff, writes nothing
  --check      writes nothing; names every public item without a comment, every `//` comment still
               to convert, every enumerator of a documented enum without one, every QDoc comment line
               longer than 100 characters, and every undocumented variable right below a documented
               one ("shares the comment of X?": confirm with \\qcshared in that comment, or give it
               its own); exit code 1 if any. Need no comment: constructors, destructors, overrides
               (the docs mark them as reimplementations), `= default`/`= delete`, the READ, WRITE,
               NOTIFY and RESET functions of a documented Q_PROPERTY, variables covered by a
               \\qcshared comment, the fields of a struct whose comment has \\qcfields (also no
               "shares the comment of X?" for them), and out-of-line definitions of members declared
               in the same file (the declaration carries the comment).
  --skip-internal  with --check: leaves out everything in a type whose comment has \\internal
  --params     names, per documented function, the parameters its comment does not name with \\a
               (and how often each stands there as a plain word): a list to review
  --apply      with --params: writes \\a for plain mentions of parameters whose name cannot be an
               English word (camelCase, digits, underscores); the rest stays a proposal
  --self-test  converts and checks samples in memory and fails on a wrong result
Folders are searched for .h and .qml files.
"""

import difflib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qc_doc_comments as docs  # noqa: E402
import qc_doc_format as fmt  # noqa: E402


def convertible(item):
    comment = item.comment
    return (comment is not None and comment.style == "line" and not docs.is_license(comment)
            and item.is_public() and (item.kind in docs.DOCUMENTED_KINDS or item.kind.startswith("qml")
                                      or item.kind == "enumerator"))


def items_of(text, path):
    if path.endswith(".qml"):
        return [item for item in docs.scan_qml(text, path) if item.kind != "qmlenum"]
    items = []
    for item in docs.scan_header(text, path):
        items.append(item)
        if item.kind == "enum" and item.is_public():
            for enumerator in item.enumerators:
                enumerator.access = item.access
                items.append(enumerator)
    return items


def replaced(text, replacements):
    """`text` with line ranges replaced: (first, last, new lines), the first one per start wins."""
    lines = text.split("\n")
    done = set()
    for start, end, block in sorted(replacements, key=lambda r: r[0], reverse=True):
        if start in done:
            continue
        done.add(start)
        lines[start:end + 1] = block
    return "\n".join(lines)


def indent_of(lines, comment):
    return re.match(r"\s*", lines[comment.start]).group(0)


def convert_text(text, path):
    """The converted text of a header or QML file."""
    lines = text.split("\n")
    replacements = []
    for item in items_of(text, path):
        comment = item.comment
        if convertible(item):
            replacements.append((comment.start, comment.end,
                                 fmt.comment_block(item, lines, indent_of(lines, comment), convert=True)))
        elif comment is not None and fmt.can_reflow(comment) and fmt.too_long(lines, comment):
            replacements.append((comment.start, comment.end,
                                 fmt.comment_block(item, lines, indent_of(lines, comment), convert=False)))
    return replaced(text, replacements)


def code_of(text, path):
    """The code of a source without comments and whitespace (scripts/qc_code_lines.py)."""
    return "".join(docs.qc_code_lines.masked(text, path).split())


def is_reimplementation(item):
    return re.search(r"\b(override|final)\b", item.text) is not None


def out_of_line_name(item):
    """(scope, name) of the member that an out-of-line function definition
    (`inline void QcA::f(int x) {…}`) defines, or None for other items."""
    if item.kind != "function":
        return None
    match = re.search(r"((?:\w+::)+)(~?\w+|operator[^\w\s(]+)\s*\(", item.text.split("(", 1)[0] + "(")
    if not match:
        return None
    return tuple(item.scope + match.group(1).rstrip(":").split("::")), match.group(2)


def exempt_names(items):
    """(scope, name) of the functions and variables that need no comment: property accessors of a
    documented property, the fields of a struct with \\qcfields, and out-of-line definitions of
    members declared in the same file (the declaration carries the comment)."""
    exempt = set()
    declared = {(tuple(item.scope), item.name) for item in items if item.kind == "function"}
    for item in items:
        defined = out_of_line_name(item)
        if defined is not None and defined in declared:
            exempt.add(id(item))
        if item.kind == "property" and item.comment is not None:
            exempt.update((tuple(item.scope), name) for name in item.accessors)
        if item.kind == "struct" and docs.has_marker(item.comment, "\\qcfields"):
            exempt.update((tuple(item.scope + [item.name]), other.name) for other in items
                          if other.kind == "variable" and other.scope == item.scope + [item.name])
    return exempt


def is_exempt(item, exempt):
    return (tuple(item.scope), item.name) in exempt or id(item) in exempt


def needs_no_comment(item, exempt):
    if is_exempt(item, exempt):
        return True
    if item.kind == "function":
        name = item.name.lstrip("~")
        return (name == (item.scope[-1] if item.scope else "") or is_reimplementation(item)
                or re.search(r"=\s*(delete|default)\s*$", item.text) is not None)
    return item.kind in ("qmlenum",)


def internal_scopes(items):
    return [item.scope + [item.name] for item in items
            if item.kind in docs.TYPE_KINDS and docs.has_marker(item.comment, "\\internal")]


def check_text(text, path, skip_internal=False):
    """Findings of --check: (line, message)."""
    lines = text.split("\n")
    items = items_of(text, path)
    exempt = exempt_names(items)
    shared = {id(variable): (owner, confirmed) for variable, owner, confirmed in docs.shared_variables(items)}
    internal = internal_scopes(items) if skip_internal else []
    findings = []
    for item in items:
        if not item.is_public() or item.kind == "enumerator":
            continue
        if item.kind not in docs.DOCUMENTED_KINDS and not item.kind.startswith("qml"):
            continue
        if item.kind == "namespace" and not item.name:
            continue
        if any(item.scope[:len(scope)] == scope or item.scope + [item.name] == scope for scope in internal):
            continue
        name = item.qualified()
        if item.comment is None or docs.is_license(item.comment):
            if id(item) in shared and not is_exempt(item, exempt):
                owner, confirmed = shared[id(item)]
                if not confirmed:
                    findings.append((item.start + 1, f"{item.kind} {name}: no comment; shares the comment of "
                                     f"{owner.name}? confirm with \\qcshared there or give it its own"))
            elif not needs_no_comment(item, exempt):
                findings.append((item.start + 1, f"{item.kind} {name}: no comment"))
        elif item.comment.style == "line":
            findings.append((item.comment.start + 1, f"{item.kind} {name}: // comment to convert"))
        elif fmt.too_long(lines, item.comment):
            findings.append((item.comment.start + 1, f"{item.kind} {name}: comment line longer than {fmt.WIDTH}"))
        if item.kind == "enum" and item.comment is not None:
            for enumerator in item.enumerators:
                if enumerator.comment is None:
                    findings.append((enumerator.start + 1, f"enumerator {name}::{enumerator.name}: no comment"))
    return findings


def parameters_text(text, path, apply):
    """--params: (proposal lines, new text with the automatic ones applied)."""
    lines = text.split("\n")
    report = []
    replacements = []
    for item in items_of(text, path):
        if not item.is_public() or item.comment is None or item.comment.style != "qdoc":
            continue
        proposals = fmt.parameter_proposals(item)
        for name, mentions, automatic in proposals:
            state = "applied" if automatic and apply else ("automatic with --apply" if automatic else "review")
            report.append(f"{path}:{item.comment.start + 1}: {item.qualified()}: \\a {name} missing "
                          f"({mentions} plain mention(s), {state})")
        names = [name for name, _, automatic in proposals if automatic]
        if apply and names:
            comment = item.comment
            old = lines[comment.start:comment.end + 1]
            new = [fmt.with_parameters([line], names)[0] for line in old]
            replacements.append((comment.start, comment.end, new))
    return report, replaced(text, replacements)


def files_of(arguments):
    for argument in arguments:
        if os.path.isdir(argument):
            for directory, dirs, files in os.walk(argument):
                dirs.sort()
                for name in sorted(files):
                    if name.endswith((".h", ".qml")):
                        yield os.path.join(directory, name)
        else:
            yield argument


SAMPLE = r"""// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// A sample class (decision E-1). It has a second sentence that goes on for a while, so that
// the rewrap has something to do.
class QcSample : public QObject
{
    Q_OBJECT
    // The name, e.g. "x".
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

public:
    enum class Kind {
        // Small one.
        Small,
        Big, // trailing stays
    };
    // Opens with `enabled` and `other()`; path C:\x, `a/*.json`, b/*.png and c*/d, `f({})`.
    // NOLINTNEXTLINE(readability-x)
    void start(bool enabled);
    QString name() const;
    void setName(const QString &name);
    // Whether the sample is open.
    bool isOpen() const;
    // The width in blocks.
    int width() const;
    // Adds `other` to this.
    QcSample &operator+=(const QcSample &other);
    bool operator==(const QcSample &) const = default;
    // Waits up to maxWaitMs for the count of items.
    void wait(int maxWaitMs, int count);

    // A section comment, separated.

    int count = 0;
    // Pause between pieces, in seconds.
    static constexpr int minPause = 60;
    static constexpr int maxPause = 180;

signals:
    void nameChanged();

private:
    // Private stays.
    void hidden();
};
"""

SAMPLE_CONVERTED = r"""// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief A sample class (decision E-1).

    It has a second sentence that goes on for a while, so that the rewrap has something to do. */
class QcSample : public QObject
{
    Q_OBJECT
    /*! The name, e.g. "x". */
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

public:
    enum class Kind {
        /*! Small one. */
        Small,
        Big, // trailing stays
    };
    /*! Opens with \a enabled and \c {other()}; path C:\\x, \c {a/}\c {*.json},
        b/\unicode {0x2A}.png and c\unicode {0x2A}/d, \c {f({})}. */
    // NOLINTNEXTLINE(readability-x)
    void start(bool enabled);
    QString name() const;
    void setName(const QString &name);
    /*! Returns whether the sample is open. */
    bool isOpen() const;
    /*! Returns the width in blocks. */
    int width() const;
    /*! Adds \a other to this. */
    QcSample &operator+=(const QcSample &other);
    bool operator==(const QcSample &) const = default;
    /*! Waits up to maxWaitMs for the count of items. */
    void wait(int maxWaitMs, int count);

    // A section comment, separated.

    int count = 0;
    /*! Pause between pieces, in seconds. */
    static constexpr int minPause = 60;
    static constexpr int maxPause = 180;

signals:
    void nameChanged();

private:
    // Private stays.
    void hidden();
};
"""

QML_SAMPLE = """import QtQuick

// A plate. It shows `text`.
Rectangle {
    id: plate

    // The text on it.
    property alias text: label.text
    // Sent on a click with `x`.
    signal clicked(int x)

    Item {
        // Inner stays.
        property int y: 0
    }
}
"""

QML_CONVERTED = r"""import QtQuick

/*! \brief A plate.

    It shows \c {text}. */
Rectangle {
    id: plate

    /*! The text on it. */
    property alias text: label.text
    /*! Sent on a click with \a x. */
    signal clicked(int x)

    Item {
        // Inner stays.
        property int y: 0
    }
}
"""

# Markers, internal types, long QDoc lines and a sentence with dots.
MARKED = """#pragma once
/*! \\brief Rows 46.. 47 hold the sample... which is fine. Second sentence. */
struct QcRows
{
    /*! The first row; this comment line is much too long for the limit of one hundred characters, so it is wrapped. */
    int first = 0;
    int second = 0;
    /*! Both bounds. \\qcshared */
    int low = 0;
    int high = 1;
};
/*! \\brief Plain fields. \\qcfields */
struct QcPlain
{
    /*! The first field. */
    int a = 0;
    int b = 0;
    /*! Adds \\a x. */
    void add(int x);
    /*! Returns whether \\a other holds the same. */
    bool operator==(const QcPlain &other) const;
};
inline void QcPlain::add(int x)
{
    a += x;
}
inline bool QcPlain::operator==(const QcPlain &other) const
{
    return a == other.a && b == other.b;
}
/*! \\brief Hidden. \\internal */
class QcHidden
{
public:
    void run();
};
"""


def compare(name, expected, result):
    if result == expected:
        return 0
    print(f"{name}: wrong result\n" + "\n".join(difflib.unified_diff(expected.split("\n"), result.split("\n"),
                                                                    "expected", "result", lineterm="")))
    return 1


def self_test():
    failures = compare("convert", SAMPLE_CONVERTED, convert_text(SAMPLE, "QcSample.h"))
    if convert_text(SAMPLE_CONVERTED, "QcSample.h") != SAMPLE_CONVERTED:
        failures += 1
        print("convert: not idempotent")
    found = [message for _, message in check_text(SAMPLE_CONVERTED, "QcSample.h")]
    wanted = ["enum QcSample::Kind: no comment", "variable QcSample::count: no comment",
              "variable QcSample::maxPause: no comment; shares the comment of minPause? confirm with \\qcshared there "
              "or give it its own"]
    if found != wanted:
        failures += 1
        print(f"check: {found}\n  instead of {wanted}")
    failures += compare("qml", QML_CONVERTED, convert_text(QML_SAMPLE, "Plate.qml"))
    report, applied = parameters_text(SAMPLE_CONVERTED, "QcSample.h", apply=True)
    if "/*! Waits up to \\a maxWaitMs for the count of items. */" not in applied:
        failures += 1
        print("params: maxWaitMs not applied")
    if not any("\\a count missing (1 plain mention(s), review)" in line for line in report):
        failures += 1
        print(f"params: count not proposed for review: {report}")
    marked = convert_text(MARKED, "QcRows.h")
    if "    /*! The first row; this comment line is much too long for the limit of one hundred" \
            not in marked or "        characters, so it is wrapped. */" not in marked:
        failures += 1
        print("reflow: long QDoc line not wrapped\n" + marked)
    found = [message for _, message in check_text(MARKED, "QcRows.h", skip_internal=True)]
    wanted = ["variable QcRows::first: comment line longer than 100",
              "variable QcRows::second: no comment; shares the comment of first? confirm with \\qcshared there "
              "or give it its own"]
    if found != wanted:
        failures += 1
        print(f"check markers: {found}\n  instead of {wanted}")
    if docs.first_sentence("Rows 46.. 47 hold the sample... which is fine. Second sentence.") != \
            "Rows 46.. 47 hold the sample... which is fine.":
        failures += 1
        print("first sentence: split after dots")
    operators = [item for item in docs.scan_header(SAMPLE, "QcSample.h") if "operator" in item.name]
    if [(item.kind, item.name, item.params) for item in operators] != [("function", "operator+=", ["other"]),
                                                                       ("function", "operator==", [])]:
        failures += 1
        print(f"operators: {[(item.kind, item.name, item.params) for item in operators]}")
    values = ("struct S {\n    static constexpr QLatin1StringView key(\"lapis\");\n    int (*read)(int x) = nullptr;\n"
              "    QString outfit = QStringLiteral(\"plain\");\n};\n")
    kinds = [(item.kind, item.name) for item in docs.scan_header(values, "S.h") if item.scope]
    if kinds != [("variable", "key"), ("variable", "read"), ("variable", "outfit")]:
        failures += 1
        print(f"variables with parentheses: {kinds}")
    print("self-test: " + ("ok" if not failures else f"{failures} failure(s)"))
    return 1 if failures else 0


def main(arguments):
    if "--self-test" in arguments:
        return self_test()
    mode = "check" if "--check" in arguments else ("params" if "--params" in arguments
                                                   else ("dry-run" if "--dry-run" in arguments else "write"))
    paths = [a for a in arguments if not a.startswith("--")]
    missing = [path for path in paths if not os.path.exists(path)]
    if missing:
        print(f"no such file or folder: {', '.join(missing)}", file=sys.stderr)
        return 2
    if not paths:
        print(__doc__, file=sys.stderr)
        return 2
    findings = 0
    for path in files_of(paths):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        if mode == "check":
            for line, message in check_text(text, path, "--skip-internal" in arguments):
                print(f"{path}:{line}: {message}")
                findings += 1
            continue
        if mode == "params":
            report, new = parameters_text(text, path, "--apply" in arguments)
            for line in report:
                print(line)
        else:
            new = convert_text(text, path)
        if new == text:
            continue
        if code_of(new, path) != code_of(text, path):
            # A safety net: the tool may only change comments.
            print(f"{path}: the change would touch code, left alone", file=sys.stderr)
            findings += 1
            continue
        if mode == "dry-run":
            sys.stdout.writelines(difflib.unified_diff(text.splitlines(True), new.splitlines(True), path, path))
        else:
            with open(path, "w", encoding="utf-8") as f:
                f.write(new)
            print(f"converted {path}")
    if mode == "check":
        print(f"{findings} finding(s)", file=sys.stderr)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
