#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Text of QDoc comments in the project's form (decision E-939), used by scripts/convert-doc-comments.py.

A block starts with `/*! ` on the line of its first text line, continues 4 spaces further in and
ends with ` */` (or `*/` on a line of its own); no line is longer than WIDTH characters. `name` in
backticks becomes `\\a name` for a parameter and `\\c {name}` otherwise, `- item` lists become
\\list/\\li/\\endlist, the comment markers `/*` and `*/` never appear in the text. Types get `\\brief`
with their first sentence as a paragraph of its own; a function with a result gets "Returns"
where its comment starts with "The", "Whether" or "True if/when".
"""

import os
import re
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qc_doc_comments as docs  # noqa: E402

WIDTH = 100
BACKTICK = re.compile(r"`([^`\n]+)`")
LIST_LINE = re.compile(r"^\s*(?:[-*•]|\d+[.)])\s|^\s{2,}\S")
LIST_ITEM = re.compile(r"^(\s*)([-*•]|\d+[.)])\s+(.*)$")
UNBREAKABLE = re.compile(r"\\(?:c|unicode|a)\s+\{(?:[^{}]|\{[^{}]*\})*\}|\\a\s+\w+")
SEPARATOR = re.compile(r"^\\(?:list|endlist|value|li)\b")
VERBATIM = re.compile(r"\\(?:code|badcode|table|snippet|qml|raw)\b")
PLAIN_START = ((re.compile(r"^The\b"), "Returns the"), (re.compile(r"^Whether\b"), "Returns whether"),
               (re.compile(r"^True (if|when)\b"), r"Returns \\c true \1"))
RETURN_WORD = re.compile(r"\breturns?\b", re.IGNORECASE)
CODE_SPAN = re.compile(r"\\[ac]\s+\{(?:[^{}]|\{[^{}]*\})*\}|\\[ac]\s+\S+|`[^`]*`")


def balanced(text):
    depth = 0
    for char in text:
        depth += {"{": 1, "}": -1}.get(char, 0)
        if depth < 0:
            return False
    return depth == 0


def markup(line, params):
    """A comment line in QDoc markup: backslashes doubled, backticks as \\a or \\c, no /* or */."""
    line = line.replace("\\", "\\\\")

    def replace(match):
        code = match.group(1)
        if code in params:
            return "\\a " + code
        if not balanced(code):
            return match.group(0)
        # A code span split between `/` and `*` never holds the comment markers /* and */.
        code = code.replace("/*", "/}\\c {*").replace("*/", "*}\\c {/")
        return "\\c {" + code + "}"
    line = BACKTICK.sub(replace, line)
    # In plain text the * of /* and */ is written as \unicode {0x2A}: the comment must not end.
    return line.replace("/*", "/\\unicode {0x2A}").replace("*/", "\\unicode {0x2A}/")


def wrap(text, width, subsequent=""):
    """`text` wrapped at `width`, never inside `\\c {...}`, `\\unicode {...}` or `\\a name`."""
    protected = UNBREAKABLE.sub(lambda match: match.group(0).replace(" ", "\x00"), text)
    lines = textwrap.wrap(protected, width, break_on_hyphens=False, break_long_words=False,
                          subsequent_indent=subsequent)
    return [line.replace("\x00", " ") for line in lines]


def split_brief(lines, width):
    """The lines of a type comment with `\\brief` and the first sentence as a paragraph of its own."""
    run = []
    for line in lines:
        if not line.strip() or (run and LIST_LINE.match(line)):
            break
        run.append(line)
    rest_lines = lines[len(run):]
    joined = " ".join(" ".join(run).split())
    sentence = docs.first_sentence(joined)
    remainder = joined[len(sentence):].strip()
    if not remainder:
        body = ["\\brief " + run[0]] + run[1:]
    else:
        body = wrap("\\brief " + sentence, width) + [""] + wrap(remainder, width)
    if rest_lines and rest_lines[0].strip():
        body.append("")
    return body + rest_lines


def qdoc_lists(lines):
    """Lines with `- item` (or `1. item`) lists as \\list ... \\li ... \\endlist."""
    out = []
    i = 0
    while i < len(lines):
        match = LIST_ITEM.match(lines[i])
        if not match:
            out.append(lines[i])
            i += 1
            continue
        indent = len(match.group(1))
        out.append("\\list 1" if match.group(2)[0].isdigit() else "\\list")
        while i < len(lines):
            match = LIST_ITEM.match(lines[i])
            if match and len(match.group(1)) == indent:
                out.append("\\li " + match.group(3))
            elif lines[i].strip() and len(lines[i]) - len(lines[i].lstrip()) > indent:
                out.append("    " + lines[i].strip())
            else:
                break
            i += 1
        out.append("\\endlist")
    return out


def fit(lines, width):
    """`lines` with each paragraph or \\li item that has a line longer than its limit wrapped anew
    (the last one leaves room for the closing ` */`); other paragraphs keep their line breaks."""
    out = []
    run = []

    def flush(last=False):
        limit = width - 3 if last else width
        if run and any(len(line) > limit for line in run):
            continuation = "    " if run[0].startswith("\\li ") else ""
            out.extend(wrap(" ".join(" ".join(run).split()), limit, continuation))
        else:
            out.extend(run)
        run.clear()
    for line in lines:
        in_item = bool(run) and run[0].startswith("\\li ")
        if line.startswith("\\li "):
            flush()
            run.append(line)
        elif in_item and line.startswith("    ") and line.strip():
            run.append(line)
        elif not line.strip() or SEPARATOR.match(line) or LIST_LINE.match(line):
            flush()
            out.append(line)
        else:
            if in_item:
                flush()
            run.append(line)
    flush(last=True)
    return out


def render(lines, indent):
    """A `/*! ... */` block for text lines at `indent`."""
    out = [indent + "/*! " + lines[0]]
    out += [(indent + "    " + line) if line else "" for line in lines[1:]]
    if len(out[-1]) + 3 <= WIDTH and out[-1].strip():
        out[-1] += " */"
    else:
        out.append(indent + "*/")
    return out


def return_type(item):
    """The return type of a function item as written (empty for constructors and destructors)."""
    head = re.sub(r"^template\s*<.*?>\s*", "", item.text)
    head = re.sub(r"\[\[[^\]]*\]\]|\b(?:Q_INVOKABLE|virtual|static|explicit|inline|constexpr|friend)\b", " ", head)
    position = head.find(item.name + "(")
    if position < 0:
        position = head.find(item.name + " (")
    return " ".join(head[:max(position, 0)].split())


def with_returns(lines, item):
    """The comment lines of a function with a result, starting with "Returns" where the first word
    makes that unambiguous ("The", "Whether", "True if/when") and the text names no return yet."""
    if item.kind != "function" or item.section == "signals" or not lines:
        return lines
    result = return_type(item)
    if result in ("", "void") or RETURN_WORD.search(" ".join(lines)):
        return lines
    for pattern, replacement in PLAIN_START:
        if pattern.match(lines[0]):
            return [pattern.sub(replacement, lines[0], count=1)] + lines[1:]
    return lines


def comment_block(item, lines, indent, convert):
    """The rendered block of `item`'s comment: `convert` for a `//` comment (markup, brief, lists,
    Returns), else a QDoc comment only fitted to the width."""
    text = list(item.comment.lines)
    if convert:
        text = [markup(line, set(item.params)) for line in text]
    while text and not text[-1].strip():
        text.pop()
    if convert:
        text = with_returns(text, item)
        if item.kind in docs.TYPE_KINDS + ("qmltype",):
            text = split_brief(text, WIDTH - len(indent) - 4)
        text = qdoc_lists(text)
    return render(fit(text, WIDTH - len(indent) - 4), indent)


def too_long(lines, comment):
    """True when a line of the comment block is longer than WIDTH."""
    return any(len(lines[number]) > WIDTH for number in range(comment.start, comment.end + 1))


def can_reflow(comment):
    """QDoc comments without verbatim parts or topics can be wrapped anew."""
    text = comment.text()
    return comment.style == "qdoc" and not VERBATIM.search(text) and not docs.TOPIC.search(text)


def plain_mentions(lines, name):
    """The number of times `name` stands as a plain word in the text (not as \\a, \\c or code)."""
    text = CODE_SPAN.sub(" ", " ".join(lines))
    return len(re.findall(r"(?<![\w\\.])" + re.escape(name) + r"(?![\w(])", text))


def parameter_proposals(item):
    """Parameters of a documented function not named with \\a: (name, plain mentions, applied
    automatically). Applied only for names that cannot be an English word (camelCase, digits,
    underscores) and appear as a plain word."""
    if item.kind != "function" or item.comment is None:
        return []
    text = " ".join(item.comment.lines)
    proposals = []
    for name in item.params:
        if re.search(r"\\a\s+" + re.escape(name) + r"\b", text):
            continue
        mentions = plain_mentions(item.comment.lines, name)
        code_like = re.search(r"[A-Z0-9_]", name) is not None
        proposals.append((name, mentions, code_like and mentions > 0))
    return proposals


def with_parameters(lines, names):
    """The comment lines with each plain mention of `names` as `\\a name`."""
    out = []
    for line in lines:
        pieces = re.split(r"(" + CODE_SPAN.pattern + r")", line)
        for index in range(0, len(pieces), 2):
            for name in names:
                pieces[index] = re.sub(r"(?<![\w\\.])" + re.escape(name) + r"(?![\w(])", "\\\\a " + name, pieces[index])
        out.append("".join(piece for piece in pieces if piece is not None))
    return out
