#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Writes the code map for agents and people new to the code (not part of the build):

  docs/Code-Karte.md          every class, struct and enum of src/ with the first sentence of the
                              comment above it, file and line; QML files, test files and scripts
  docs/Entscheidungen-Index.md one line per decision of docs/Entscheidungen.md with the classes it
                              names

Both files are generated and not in git (merge conflicts between branches); CLAUDE.md says when
to run this. Takes about a second.
Usage: scripts/code-map.py [--check]   (--check: exit 1 when a file would change, write nothing)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CODE_MAP = os.path.join(ROOT, "docs", "Code-Karte.md")
DECISION_INDEX = os.path.join(ROOT, "docs", "Entscheidungen-Index.md")

# A declaration at namespace level that opens a body (forward declarations end with ';').
DECLARATION = re.compile(
    r"^(?:template\s*<[^>]*>\s*)?(?:(class|struct|enum class|enum)\s+(\w+)\b(?!\s*;)"
    r"|(namespace)\s+(Qc\w+)\s*\{|(using)\s+(Qc\w+)\s*=)")
SENTENCE_END = re.compile(r"(?<=[.!?])\s")
MAX_SUMMARY = 160


def first_sentence(lines):
    text = " ".join(" ".join(lines).split())
    if not text:
        return ""
    # "e.g." and "i.e." do not end a sentence.
    match = next((m for m in SENTENCE_END.finditer(text) if not text[: m.start()].endswith(("e.g.", "i.e."))), None)
    sentence = text[: match.start()] if match else text
    if len(sentence) > MAX_SUMMARY:
        sentence = sentence[: MAX_SUMMARY - 1].rstrip() + "…"
    return sentence


def comment_above(lines, index):
    """The block of // comment lines directly above line `index` (blank lines end it)."""
    block = []
    i = index - 1
    while i >= 0 and lines[i].lstrip().startswith("//"):
        block.insert(0, lines[i].lstrip()[2:].lstrip("/").strip())
        i -= 1
    return block


def header_entries(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    entries = []
    for i, line in enumerate(lines):
        match = DECLARATION.match(line)
        if not match:
            continue
        kind, name = next((match.group(k), match.group(k + 1)) for k in (1, 3, 5) if match.group(k))
        if kind == "namespace" and any(entry[0] == name for entry in entries):
            continue
        # The comment of a template stands above its template line.
        start = i - 1 if i > 0 and lines[i - 1].lstrip().startswith("template") else i
        entries.append((name, kind, i + 1, first_sentence(comment_above(lines, start))))
    return entries


def leading_comment(path, marker):
    """First sentence of the first comment block of a file (after a shebang or pragma)."""
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    skipped = ("#!", "#pragma", "#include", "import ", "pragma ")
    block = []
    for line in lines:
        stripped = line.strip()
        if "SPDX-" in stripped and not block:
            continue  # the licence header (scripts/add-spdx.py) is no description
        if stripped.startswith(marker) and not stripped.startswith("#!"):
            block.append(stripped[len(marker):].lstrip("/#").strip())
        elif block:
            break
        elif stripped and not stripped.startswith(skipped):
            break
    return first_sentence(block)


def rel(path):
    return os.path.relpath(path, ROOT)


def link(path, line=None):
    # File name only: the heading above names the directory.
    return f"{os.path.basename(path)}:{line}" if line else os.path.basename(path)


def walk(top, suffixes):
    result = {}
    for directory, dirs, files in os.walk(top):
        dirs.sort()
        chosen = sorted(f for f in files if f.endswith(suffixes))
        if chosen:
            result[directory] = [os.path.join(directory, f) for f in chosen]
    return dict(sorted(result.items()))


def code_map():
    out = [
        "# Code-Karte",
        "",
        "Generiert von `scripts/code-map.py` (nicht von Hand bearbeiten, nicht in git). Pro Verzeichnis jede Klasse,",
        "jedes Struct, Enum, jeden Namensraum und Typ-Alias (`Qc…`) aus den Headern (Datei:Zeile, Verzeichnis in der Überschrift) mit dem ersten",
        "Satz des Kommentars darüber; danach QML, Tests und Skripte. Abschnitt per grep suchen statt die Datei",
        "ganz zu lesen; für Definitionen, Verwendungen und Aufrufer das LSP-Werkzeug (clangd) nehmen.",
        "",
    ]
    for directory, files in walk(os.path.join(ROOT, "src"), (".h", ".qml")).items():
        out.append(f"## {rel(directory)}")
        out.append("")
        for path in files:
            if path.endswith(".qml"):
                summary = leading_comment(path, "//")
                out.append(f"- {link(path)}" + (f" — {summary}" if summary else ""))
                continue
            entries = header_entries(path)
            if not entries:
                summary = leading_comment(path, "//")
                out.append(f"- {link(path)}" + (f" — {summary}" if summary else ""))
            for name, kind, line, summary in entries:
                label = "" if kind in ("class", "struct") else f" ({kind})"
                out.append(f"- `{name}`{label} {link(path, line)}" + (f" — {summary}" if summary else ""))
        out.append("")

    out.append("## Tests")
    out.append("")
    for directory, files in walk(os.path.join(ROOT, "tests"), (".cpp",)).items():
        names = ", ".join(os.path.splitext(os.path.basename(f))[0] for f in files)
        out.append(f"- `{rel(directory)}`: {names}")
    out.append("")

    out.append("## Skripte")
    out.append("")
    for path in walk(os.path.join(ROOT, "scripts"), (".sh", ".py")).get(os.path.join(ROOT, "scripts"), []):
        summary = leading_comment(path, "#") if path.endswith(".sh") else python_docstring(path)
        out.append(f"- {link(path)}" + (f" — {summary}" if summary else ""))
    out.append("")
    return "\n".join(out)


def python_docstring(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    match = re.search(r'"""(.*?)"""', text, re.S)
    return first_sentence(match.group(1).splitlines()) if match else leading_comment(path, "#")


def decision_index():
    path = os.path.join(ROOT, "docs", "Entscheidungen.md")
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    heading = re.compile(r"^## (E-\d+)\s+(.*)$")
    identifier = re.compile(r"`(Qc\w+(?:::\w+)?)")
    out = [
        "# Entscheidungen – Index",
        "",
        "Generiert von `scripts/code-map.py` aus `docs/Entscheidungen.md` (nicht von Hand bearbeiten, nicht in git).",
        "Eine Zeile pro Entscheidung: Nummer mit Link, Titel, die ersten genannten Klassen. Erst hier suchen,",
        "dann nur den passenden Abschnitt lesen.",
        "",
    ]
    current = None
    for number, line in enumerate(lines, 1):
        match = heading.match(line)
        if match:
            if current:
                out.append(format_decision(current))
            current = {"id": match.group(1), "title": match.group(2).strip(), "line": number, "names": []}
        elif current:
            for name in identifier.findall(line):
                if name not in current["names"]:
                    current["names"].append(name)
    if current:
        out.append(format_decision(current))
    out.append("")
    return "\n".join(out)


def format_decision(decision):
    names = ", ".join(f"`{n}`" for n in decision["names"][:6])
    more = " …" if len(decision["names"]) > 6 else ""
    return f"- [{decision['id']}](Entscheidungen.md:{decision['line']}) {decision['title']}" + (f" — {names}{more}" if names else "")


def main():
    check = "--check" in sys.argv[1:]
    stale = []
    for path, text in ((CODE_MAP, code_map()), (DECISION_INDEX, decision_index())):
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old == text:
            continue
        stale.append(rel(path))
        if not check:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
    if check and stale:
        print("out of date: " + ", ".join(stale) + " (run scripts/code-map.py)", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
