#!/usr/bin/env python3
"""Code lines of C++, Objective-C++ and QML sources: the unit of the size limits (E-760).

A code line is a line with at least one character outside a comment: blank lines, `//` lines and lines inside
`/* */` blocks (doc comments included) do not count, nor the line after a `//` comment that ends in a
backslash; a line with code and a trailing comment counts. String,
character, raw string (`R"x(...)x"`) and, in QML, template literals are code, so a comment marker inside them
is no comment; a `'` inside a number (`1'000`) is a digit separator. Preprocessor lines are code.
Used by scripts/check-file-length.sh (file lengths) and scripts/check-function-length.py (function lengths).
Usage: scripts/qc_code_lines.py <file> ...   (prints "<code lines> <file>" per file, like `wc -l`)
"""

import re
import sys

SPECIAL_CPP = re.compile(r"//|/\*|\"|'")
SPECIAL_QML = re.compile(r"//|/\*|\"|'|`")
STRING_LITERAL = {
    '"': re.compile(r'"(?:[^"\\\n]|\\.)*"?'),
    "'": re.compile(r"'(?:[^'\\\n]|\\.)*'?"),
    "`": re.compile(r"`(?:[^`\\]|\\.)*`?", re.DOTALL),
}
RAW_STRING_PREFIXES = {"R", "u8R", "uR", "UR", "LR"}
IDENTIFIER_CHARS = re.compile(r"[A-Za-z0-9_]")
NUMBER_TOKEN_CHARS = re.compile(r"[A-Za-z0-9_'.]")
NOT_NEWLINE = re.compile(r"[^\n]")


def is_qml(path):
    return path.endswith((".qml", ".js"))


def blank(text):
    """The text with every character except the line breaks replaced by a space."""
    return NOT_NEWLINE.sub(" ", text)


def placeholder(literal):
    """A string literal with its quotes kept and its content replaced by `x` (still code, no braces)."""
    if len(literal) < 2:
        return literal
    return literal[0] + NOT_NEWLINE.sub("x", literal[1:-1]) + literal[-1]


def token_start(text, end, chars):
    """The start of the run of `chars` that ends before `end`."""
    start = end
    while start > 0 and chars.match(text[start - 1]):
        start -= 1
    return start


def raw_string_end(text, quote):
    """The end of the raw string whose `"` is at `quote`, or None when no raw string prefix precedes it."""
    prefix = text[token_start(text, quote, IDENTIFIER_CHARS):quote]
    if prefix not in RAW_STRING_PREFIXES:
        return None
    open_paren = text.find("(", quote)
    if open_paren < 0:
        return None
    closing = ")" + text[quote + 1:open_paren] + '"'
    close = text.find(closing, open_paren)
    return len(text) if close < 0 else close + len(closing)


def is_digit_separator(text, quote):
    """True when the `'` at `quote` is inside a number literal (`1'000'000`, `0xFF'FF`)."""
    start = token_start(text, quote, NUMBER_TOKEN_CHARS)
    return start < quote and text[start].isdigit()


def comment_end(text, start, qml):
    """The end of the comment starting at `start` (a `//` comment ends before its line break; in C++
    a backslash at its end continues it)."""
    if text.startswith("//", start):
        # A backslash at the end of the line continues the comment on the next line.
        end = text.find("\n", start)
        while not qml and end > 0 and text[end - 1 - (text[end - 1] == "\r")] == "\\":
            end = text.find("\n", end + 1)
        return len(text) if end < 0 else end
    end = text.find("*/", start + 2)
    return len(text) if end < 0 else end + 2


def literal_end(text, start, qml):
    """The end of the string or character literal starting at `start`."""
    if not qml and text[start] == '"':
        raw_end = raw_string_end(text, start)
        if raw_end is not None:
            return raw_end
    return STRING_LITERAL[text[start]].match(text, start).end()


def masked(text, path):
    """The text with comments blanked out and literal contents replaced by `x`; line breaks stay in place."""
    qml = is_qml(path)
    special = SPECIAL_QML if qml else SPECIAL_CPP
    parts = []
    position = 0
    while True:
        match = special.search(text, position)
        if match is None:
            parts.append(text[position:])
            return "".join(parts)

        start = match.start()
        parts.append(text[position:start])
        if match.group() in ("//", "/*"):
            end = comment_end(text, start, qml)
            parts.append(blank(text[start:end]))
        elif match.group() == "'" and not qml and is_digit_separator(text, start):
            end = start + 1
            parts.append("'")
        else:
            end = literal_end(text, start, qml)
            parts.append(placeholder(text[start:end]))
        position = end


def code_line_flags(masked_text):
    """One flag per line of the masked text: True when the line holds code."""
    return [bool(line.strip()) for line in masked_text.split("\n")]


def count_code_lines(text, path):
    """The number of code lines of a source text."""
    return sum(code_line_flags(masked(text, path)))


def read_source(path):
    with open(path, encoding="utf-8", errors="replace") as file:
        return file.read()


def main(paths):
    for path in paths:
        print(f"{count_code_lines(read_source(path), path)} {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
