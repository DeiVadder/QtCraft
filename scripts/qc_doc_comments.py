#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Declarations of C++ headers and QML files with the comment above each (API documentation, E-939).

A heuristic scanner, not a parser: comments, literals and preprocessor lines are blanked out
(scripts/qc_code_lines.py), then braces, parentheses, `;`, access specifiers and macro lines are
followed through namespaces, classes, structs and enums. It finds classes, structs, unions,
namespaces, enums with their enumerators, functions, variables, type aliases, Q_PROPERTY lines and
QML root objects with their properties, signals and functions, each with its scope, access and the
comment block directly above it (`//` lines or a `/* */` block; lines with `NOLINT` or
`clang-format` markers between comment and declaration are skipped).
Used by scripts/convert-doc-comments.py (turns `//` comments into QDoc comments), scripts/api-docs.py
(adds the QDoc topic commands before qdoc runs) and scripts/code-map.py (first sentence).
"""

import os
import re
import sys
from dataclasses import dataclass, field

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qc_code_lines  # noqa: E402

TYPE_KINDS = ("class", "struct", "union", "namespace", "enum")
DOCUMENTED_KINDS = TYPE_KINDS + ("function", "variable", "property", "typealias", "typedef")
MARKER = re.compile(r"^\s*//\s*(NOLINT|clang-format)")
TOPIC = re.compile(r"\\(fn|class|struct|union|enum|variable|property|typedef|typealias|namespace|macro|"
                   r"headerfile|page|module|qmltype|qmlproperty|qmlsignal|qmlmethod|qmlmodule|group|"
                   r"qmlvaluetype|qmlattachedproperty|qmlattachedsignal|qmlattachedmethod|externalpage)\b")
ACCESS = re.compile(r"(public|protected|private)(\s+(?:slots|Q_SLOTS))?\s*:(?!:)|(signals|Q_SIGNALS)\s*:(?!:)")
MACRO_LINE = re.compile(r"[A-Z][A-Z0-9_]*\b")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
TYPE_HEAD = re.compile(r"^(?:template\s*<.*>\s*)?(?:(class|struct|union)|(enum)(?:\s+(?:class|struct))?|"
                       r"(namespace)|(extern)\s*\"x*\")\b(.*)$", re.S)
NOT_FUNCTIONS = {"static_assert", "decltype", "alignas", "sizeof", "noexcept", "requires", "Q_PROPERTY"}


@dataclass
class Comment:
    """A comment block: first and last line (0-based), style and its text lines without markers."""

    start: int
    end: int
    style: str  # "line" (`//`), "qdoc" (`/*!`) or "block" (`/*`)
    lines: list

    def text(self):
        return "\n".join(self.lines)


@dataclass
class Item:
    """A declaration found in a header or QML file."""

    kind: str
    name: str
    scope: list
    access: str  # "public", "protected", "private" or "" (namespace level, QML)
    section: str  # "signals", "slots" or ""
    start: int
    end: int
    text: str  # the declaration as written, comments blanked and whitespace collapsed
    comment: Comment = None
    params: list = field(default_factory=list)
    enumerators: list = field(default_factory=list)
    nested: list = field(default_factory=list)  # names of types declared in a class
    accessors: list = field(default_factory=list)  # READ, WRITE, NOTIFY, RESET functions of a property
    blank_before: bool = False  # a blank line (or the scope start) directly above, comment included

    def qualified(self):
        return "::".join(self.scope + [self.name])

    def is_public(self):
        return self.access in ("", "public", "protected")


# --- comments ---------------------------------------------------------------------------------

def comment_lines(lines, style, start, end):
    """The text lines of a comment block without `//`, `/*!`, `/*` and `*/` (a `\\brief` stays)."""
    if style == "line":
        raw = [lines[i].strip()[2:] for i in range(start, end + 1)]
        raw = [line[1:] if line.startswith(" ") else line for line in raw]
        return [line.rstrip() for line in raw]
    text = "\n".join(lines[start:end + 1]).strip()
    text = text[3:] if text.startswith("/*!") else text[2:]
    text = text[:-2] if text.endswith("*/") else text
    body = text.split("\n")
    first = body[0].strip()
    rest = body[1:]
    indent = min((len(line) - len(line.lstrip()) for line in rest if line.strip()), default=0)
    return [first] + [line[indent:].rstrip() for line in rest]


def comment_above(lines, index):
    """The comment block directly above line `index` (marker lines skipped), or None."""
    i = index - 1
    while i >= 0 and MARKER.match(lines[i]):
        i -= 1
    if i < 0 or not lines[i].strip():
        return None
    stripped = lines[i].strip()
    if stripped.startswith("//"):
        end = i
        while i >= 0 and lines[i].strip().startswith("//") and not MARKER.match(lines[i]):
            i -= 1
        start = i + 1
        if start > end:
            return None
        return Comment(start, end, "line", comment_lines(lines, "line", start, end))
    if stripped.endswith("*/"):
        end = i
        while i >= 0 and "/*" not in lines[i]:
            i -= 1
        if i < 0 or lines[i].split("/*", 1)[0].strip():
            return None
        style = "qdoc" if lines[i].strip().startswith("/*!") else "block"
        return Comment(i, end, style, comment_lines(lines, style, i, end))
    return None


def is_license(comment):
    return comment is not None and any("SPDX-" in line for line in comment.lines)


# Markers of the project in QDoc comments, read by the tools and removed before qdoc sees the text:
#   \qcshared  this comment documents the variables directly below it too (no blank line, no comment
#              of their own): a group of constants or fields it describes as a whole
#   \qcfields  on a struct: its text describes the fields, which need no comment of their own
MARKERS = ("\\qcshared", "\\qcfields")
MARKER_WORD = re.compile(r"\s*\\qc(?:shared|fields)\b")


def has_marker(comment, marker):
    return comment is not None and any(marker in line for line in comment.lines)


def without_markers(line):
    return MARKER_WORD.sub("", line)


def shared_variables(items):
    """For each undocumented public variable directly below a documented one in the same scope (no
    blank line between): (the variable, the documented variable above, whether its comment has
    \\qcshared). A run continues through undocumented variables."""
    result = []
    owner = None
    previous = None
    for item in sorted(items, key=lambda item: item.start):
        if item.kind != "variable" or not item.is_public():
            if item.kind != "enumerator":
                owner = previous = None
            continue
        documented = item.comment is not None and not is_license(item.comment)
        follows = previous is not None and previous.scope == item.scope and previous.end == item.start - 1
        if documented:
            owner = item
        elif follows and not item.blank_before and owner is not None:
            result.append((item, owner, has_marker(owner.comment, "\\qcshared")))
        else:
            owner = None
        previous = item
    return result


SENTENCE_END = re.compile(r"(?<=[.!?])\s+")
ABBREVIATIONS = ("e.g.", "i.e.", "approx.", "vs.")


def first_sentence(text):
    """The first sentence of `text`: it ends at `.`, `!` or `?` and a space, not after an
    abbreviation ("e.g.") or two dots ("...", "46.."), and only before an upper-case letter, a digit,
    a QDoc command, a quote, an opening parenthesis or a list mark."""
    for match in SENTENCE_END.finditer(text):
        before = text[:match.start()]
        after = text[match.end():]
        if before.endswith(ABBREVIATIONS) or before.endswith(".."):
            continue
        if after and not (after[0].isupper() or after[0].isdigit() or after[0] in "\\\"'(`-*•"):
            continue
        return before
    return text


# --- C++ declarations ------------------------------------------------------------------------

def blank_preprocessor(masked):
    """Preprocessor lines (with continuations) blanked out."""
    def blank(match):
        return qc_code_lines.blank(match.group(0))
    return re.sub(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", blank, masked, flags=re.M)


class Scanner:
    """Walks a masked header and collects its declarations."""

    def __init__(self, text, path="x.h"):
        self.text = text
        self.lines = text.split("\n")
        self.masked = blank_preprocessor(qc_code_lines.masked(text, path))
        self.line_starts = [0]
        for line in self.masked.split("\n"):
            self.line_starts.append(self.line_starts[-1] + len(line) + 1)
        self.items = []

    def line_of(self, position):
        low, high = 0, len(self.line_starts) - 1
        while low < high:
            middle = (low + high + 1) // 2
            if self.line_starts[middle] <= position:
                low = middle
            else:
                high = middle - 1
        return low

    def skip_space(self, position):
        while position < len(self.masked) and self.masked[position].isspace():
            position += 1
        return position

    def matching(self, position, opening, closing):
        """The position after the bracket that closes the one at `position`."""
        depth = 0
        for i in range(position, len(self.masked)):
            char = self.masked[i]
            if char == opening:
                depth += 1
            elif char == closing:
                depth -= 1
                if depth == 0:
                    return i + 1
        return len(self.masked)

    def scan(self):
        self.scan_scope(0, len(self.masked), [], "", "namespace")
        return self.items

    def scan_scope(self, position, end, scope, access, scope_kind):
        """Declarations between `position` and `end` (a namespace or class body)."""
        section = ""
        nested = []
        last_end_line = self.line_of(position) - (1 if position else 0)
        while True:
            position = self.skip_space(position)
            if position >= end:
                return nested
            rest = self.masked[position:end]
            if rest[0] in ";}":
                position += 1
                continue
            access_match = ACCESS.match(rest)
            if access_match and scope_kind == "class":
                access = access_match.group(1) or "public"
                section = "slots" if access_match.group(2) else ("signals" if access_match.group(3) else "")
                if access_match.group(3):
                    access = "public"
                position += access_match.end()
                last_end_line = self.line_of(position)
                continue
            macro_end = self.macro_statement(position, end)
            if macro_end is not None:
                self.macro_item(position, macro_end, scope, access, section, last_end_line)
                position = macro_end
                last_end_line = self.line_of(position - 1)
                continue
            item, position = self.declaration(position, end, scope, access, section)
            if item is not None:
                item.blank_before = self.blank_between(last_end_line, item)
                self.items.append(item)
                if item.kind in TYPE_KINDS and item.kind != "namespace":
                    nested.append(item.name)
            last_end_line = self.line_of(position - 1)

    def blank_between(self, last_end_line, item):
        """True when a blank line or the scope start separates the item (with its comment) from the code above."""
        first = item.comment.start if item.comment else item.start
        return first - last_end_line > 1

    def macro_statement(self, position, end):
        """The end of a macro line (`Q_OBJECT`, `Q_PROPERTY(...)`, `QML_ELEMENT`) at `position`, or None."""
        match = MACRO_LINE.match(self.masked, position)
        if not match or match.end() > end:
            return None
        after = match.end()
        probe = after
        while probe < end and self.masked[probe] in " \t":
            probe += 1
        if probe < end and self.masked[probe] == "(":
            after = self.matching(probe, "(", ")")
            probe = after
            while probe < end and self.masked[probe] in " \t":
                probe += 1
        if probe >= end or self.masked[probe] == "\n":
            return after
        if self.masked[probe] == ";" and match.group(0).startswith(("Q_", "QML_")):
            return probe + 1
        return None

    def macro_item(self, start, end, scope, access, section, last_end_line):
        text = " ".join(self.masked[start:end].split())
        match = re.match(r"Q_PROPERTY\s*\((.*)\)", text, re.S)
        if not match:
            return
        words = match.group(1).split()
        names = [w for w in words if IDENTIFIER.fullmatch(w)]
        keywords = {"READ", "WRITE", "NOTIFY", "RESET", "MEMBER", "BINDABLE", "CONSTANT", "FINAL",
                    "REQUIRED", "DESIGNABLE", "SCRIPTABLE", "STORED", "USER", "REVISION"}
        before = []
        for word in match.group(1).replace("*", " * ").replace("&", " & ").split():
            if word in keywords:
                break
            before.append(word)
        name = before[-1] if before and IDENTIFIER.fullmatch(before[-1]) else (names[0] if names else "")
        line = self.line_of(start)
        # A property is API wherever the macro stands (often above the first access specifier).
        item = Item("property", name, list(scope), "public", section, line, self.line_of(end - 1), text,
                    comment_above(self.lines, line))
        item.blank_before = (item.comment.start if item.comment else line) - last_end_line > 1
        words = match.group(1).split()
        item.accessors = [words[i + 1] for i, word in enumerate(words[:-1])
                          if word in ("READ", "WRITE", "NOTIFY", "RESET") and IDENTIFIER.fullmatch(words[i + 1])]
        self.items.append(item)

    def declaration(self, start, end, scope, access, section):
        """The declaration at `start`: returns the item (or None) and the position after it."""
        position = start
        head_end = None
        body = None
        has_init_list = False
        while position < end:
            char = self.masked[position]
            if char in "([":
                position = self.matching(position, char, ")" if char == "(" else "]")
                continue
            if char == ":" and self.masked[position:position + 2] != "::" and self.masked[position - 1] != ":":
                head = self.masked[start:position]
                if ")" in head and "(" in head and not head.rstrip().endswith(("?",)):
                    has_init_list = True
            if char == ";":
                head_end = position
                position += 1
                break
            if char == "{":
                head = self.masked[start:position]
                kind = self.brace_kind(head, has_init_list, position)
                if kind == "init":
                    position = self.matching(position, "{", "}")
                    continue
                head_end = position
                body = (position, self.matching(position, "{", "}"))
                position = body[1]
                break
            if char == "}":
                head_end = position
                break
            position += 1
        if head_end is None:
            head_end = position
        head = " ".join(self.masked[start:head_end].split())
        line = self.line_of(start)
        last = self.line_of(max(start, (body[1] if body else head_end) - 1))
        item = self.classify(head, scope, access, section, line, last)
        if item is not None and body is not None and item.kind in TYPE_KINDS:
            inner_scope = scope + [item.name] if item.kind != "namespace" or item.name else scope
            if item.kind == "enum":
                item.enumerators = self.enumerators(body[0] + 1, body[1] - 1, line)
            elif item.kind == "namespace" or item.name == "":
                self.scan_scope(body[0] + 1, body[1] - 1, inner_scope, "", "namespace")
            else:
                inner_access = "private" if item.kind == "class" else "public"
                item.nested = self.scan_scope(body[0] + 1, body[1] - 1, inner_scope, inner_access, "class")
            # A type nested in a private part is no API, and so are its members.
            if not item.is_public():
                for nested_item in self.items:
                    if nested_item.scope[:len(inner_scope)] == inner_scope and nested_item.access != "private":
                        nested_item.access = "private"
        if item is not None and item.kind in TYPE_KINDS and body is None:
            return None, position  # a forward declaration
        return item, position

    def brace_kind(self, head, has_init_list, position):
        """"body" when the `{` at `position` opens a body, "init" when it is an initialiser."""
        stripped = " ".join(head.split())
        type_match = TYPE_HEAD.match(stripped)
        # A default template argument (`template<int N = 2>`) is no initialiser.
        if type_match and "=" not in re.sub(r"^template\s*<.*?>", "", stripped).split("(")[0]:
            return "body"
        before = self.masked[:position].rstrip()
        if has_init_list:
            return "init" if before and (before[-1].isalnum() or before[-1] in "_>") else "body"
        top = without_operator_symbols(top_level(stripped))
        if "=" in top.split("(")[0]:
            return "init"
        if "(" in top:
            return "body"
        return "init"

    def classify(self, head, scope, access, section, line, last):
        """An item for a declaration head, or None for what is no API."""
        comment = comment_above(self.lines, line)
        type_match = TYPE_HEAD.match(head)
        if type_match:
            kind = type_match.group(1) or type_match.group(2) or type_match.group(3)
            if type_match.group(4):
                return Item("namespace", "", list(scope), access, section, line, last, head, None)
            rest = type_match.group(5).strip()
            rest = re.sub(r"\[\[[^\]]*\]\]|\balignas\s*\([^)]*\)|\b[A-Z][A-Z0-9_]*_EXPORT\b", "", rest).strip()
            name_match = re.match(r"([\w:]*)", rest)
            name = name_match.group(1) if name_match else ""
            if kind == "namespace":
                return Item("namespace", name, list(scope), "", section, line, last, head, comment)
            if not name:
                return None
            return Item(kind, name.split("::")[-1], list(scope), access, section, line, last, head, comment)
        if not head or head.startswith(("friend ", "using namespace", "static_assert", "template class",
                                        "extern template")):
            return None
        if head.startswith("using "):
            alias = re.match(r"using\s+(\w+)\s*=", head)
            if not alias:
                return None  # using-declaration (`using Base::Base;`)
            return Item("typealias", alias.group(1), list(scope), access, section, line, last, head, comment)
        if head.startswith("typedef "):
            name = IDENTIFIER.findall(head.split("(")[0])
            return Item("typedef", name[-1] if name else "", list(scope), access, section, line, last, head, comment)
        top = top_level(head)
        bare = without_operator_symbols(top)
        paren = bare.find("(")
        equals = bare.find("=")
        pointer = re.match(r"\(\s*[*&]\s*(\w+)\s*\)", bare[paren:]) if paren > 0 else None
        if pointer:
            # A pointer to a function (`int (*read)(int)`): a variable.
            return Item("variable", pointer.group(1), list(scope), access, section, line, last, head, comment)
        literal = re.match(r"\(\s*[\"'0-9{-]", bare[paren:]) if paren > 0 else None
        if literal:
            # Direct initialisation with a value (`QLatin1StringView key("x")`): a variable.
            names = IDENTIFIER.findall(bare[:paren])
            return Item("variable", names[-1], list(scope), access, section, line, last, head, comment) if names else None
        if paren > 0 and (equals < 0 or equals > paren):
            name = function_name(top[:paren])
            if name and name not in NOT_FUNCTIONS and not name.isupper():
                item = Item("function", name, list(scope), access, section, line, last, head, comment)
                item.params = parameter_names(head)
                return item
            return None
        name_part = re.split(r"[={\[;:]", top.replace("::", "\x00"))[0].replace("\x00", "::")
        names = IDENTIFIER.findall(name_part)
        if not names:
            return None
        return Item("variable", names[-1], list(scope), access, section, line, last, head, comment)

    def enumerators(self, start, end, enum_line):
        """The enumerators between `start` and `end` with their comments (a comment above counts for the
        first enumerator of a line below the enum's own line)."""
        result = []
        position = start
        seen_lines = {enum_line}
        for piece in split_top_level(self.masked[start:end], brackets="([{"):
            offset = self.masked.find(piece.strip(), position) if piece.strip() else -1
            position += len(piece) + 1
            name = IDENTIFIER.match(piece.strip()) if piece.strip() else None
            if not name or offset < 0:
                continue
            line = self.line_of(offset)
            comment = comment_above(self.lines, line) if line not in seen_lines else None
            seen_lines.add(line)
            trailing = trailing_comment(self.lines[line])
            if comment is None and trailing:
                comment = Comment(line, line, "trailing", [trailing])
            result.append(Item("enumerator", name.group(0), [], "", "", line, line, piece.strip(), comment))
        return result


OPERATOR_SYMBOL = re.compile(r"\boperator\s*(\(\)|\[\]|[^\s\w(]+)")


def without_operator_symbols(text):
    """`text` with the symbols of an operator name (`operator==`, `operator+=`) replaced by `#`, same
    length: an `=` there is no initialiser."""
    return OPERATOR_SYMBOL.sub(lambda match: match.group(0)[:-len(match.group(1))] + "#" * len(match.group(1)), text)


def top_level(text):
    """`text` with the content of template argument lists (`<...>`) removed, outside parentheses."""
    out = []
    depth = 0
    paren = 0
    previous = ""
    for i, char in enumerate(text):
        if char == "(":
            paren += 1
        elif char == ")":
            paren -= 1
        if paren == 0 and char == "<" and (previous.isalnum() or previous in "_>") and not text[:i].endswith("operator"):
            depth += 1
            continue
        if paren == 0 and char == ">" and depth > 0:
            depth -= 1
            previous = char
            continue
        if depth == 0:
            out.append(char)
        if not char.isspace():
            previous = char
    return "".join(out)


def function_name(before_paren):
    """The name of a function from the text before its parameter list."""
    operator = re.search(r"(operator\s*(?:\(\)|\[\]|[^\s\w(]+|\s+[\w:<>*& ]+?))\s*$", before_paren)
    if operator:
        return " ".join(operator.group(1).split())
    match = re.search(r"(~?[A-Za-z_]\w*)\s*$", before_paren)
    return match.group(1) if match else ""


def split_top_level(text, separator=",", brackets="([{<"):
    """`text` split at `separator` outside brackets."""
    closing = {"(": ")", "[": "]", "{": "}", "<": ">"}
    closers = "".join(closing[b] for b in brackets)
    parts = []
    depth = 0
    current = []
    for char in text:
        if char in brackets:
            depth += 1
        elif char in closers:
            depth -= 1
        if char == separator and depth == 0:
            parts.append("".join(current))
            current = []
            continue
        current.append(char)
    parts.append("".join(current))
    return parts


def parameter_list(head):
    """The text between the parentheses of a function head (first top-level parameter list)."""
    top = top_level(head)
    paren = top.find("(")
    if paren < 0:
        return ""
    # Find the same parenthesis in the original head: count the `(` before it in top-level text.
    prefix = top[:paren]
    name = function_name(prefix)
    index = head.find(name + "(") if name else -1
    if index < 0:
        index = head.find(name + " (")
    start = head.find("(", index + len(name)) if index >= 0 else head.find("(")
    depth = 0
    for i in range(start, len(head)):
        if head[i] == "(":
            depth += 1
        elif head[i] == ")":
            depth -= 1
            if depth == 0:
                return head[start + 1:i]
    return head[start + 1:]


def parameter_names(head):
    names = []
    for parameter in split_top_level(parameter_list(head)):
        declarator = parameter.split("=")[0].strip()
        if not declarator or declarator == "void" or declarator == "...":
            continue
        declarator = re.sub(r"\[[^\]]*\]$", "", declarator).strip()
        match = re.search(r"([A-Za-z_]\w*)\s*$", declarator)
        words = IDENTIFIER.findall(declarator)
        if match and len(words) > 1 and words[-1] not in ("const", "int", "char", "bool", "float", "double",
                                                          "long", "short", "unsigned", "signed"):
            names.append(match.group(1))
    return names


def trailing_comment(line):
    """The text of a `//` comment after code on `line`, or ""."""
    masked = qc_code_lines.masked(line, "x.h")
    code = masked.rstrip()
    if not code.strip() or len(code) >= len(line):
        return ""
    rest = line[len(code):].strip()
    return rest[2:].strip() if rest.startswith("//") else ""


def scan_header(text, path="x.h"):
    """The items of a header text."""
    return Scanner(text, path).scan()


# --- QML ---------------------------------------------------------------------------------------

QML_MEMBER = re.compile(r"^\s*(?:(?:default|required|readonly|final)\s+)*property\s+([\w.<>]+)\s+(\w+)"
                        r"|^\s*signal\s+(\w+)|^\s*function\s+(\w+)|^\s*enum\s+(\w+)")


def scan_qml(text, path="x.qml"):
    """The root object of a QML file (kind "qmltype") and its members (properties, signals, functions)."""
    lines = text.split("\n")
    masked = qc_code_lines.masked(text, path).split("\n")
    items = []
    depth = 0
    root = None
    for index, line in enumerate(masked):
        stripped = line.strip()
        if depth == 0 and root is None and re.match(r"^[A-Z][\w.]*\s*\{", stripped):
            root = Item("qmltype", os.path.splitext(os.path.basename(path))[0], [], "", "", index, index,
                        stripped, comment_above(lines, index))
            items.append(root)
        elif depth == 1 and root is not None:
            match = QML_MEMBER.match(line)
            if match:
                if match.group(2):
                    kind, name = "qmlproperty", match.group(2)
                elif match.group(3):
                    kind, name = "qmlsignal", match.group(3)
                elif match.group(4):
                    kind, name = "qmlmethod", match.group(4)
                else:
                    kind, name = "qmlenum", match.group(5)
                item = Item(kind, name, [root.name], "", "", index, index, stripped, comment_above(lines, index))
                item.params = [p.split(":")[0].strip().split()[-1] for p in
                               split_top_level(stripped.split("(", 1)[1].rsplit(")", 1)[0]) if p.strip()] \
                    if "(" in stripped and kind != "qmlproperty" else []
                items.append(item)
        depth += line.count("{") - line.count("}")
    return items
