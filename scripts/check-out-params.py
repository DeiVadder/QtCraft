#!/usr/bin/env python3
"""Lists output parameters in src/ (rule "Keine Ausgabeparameter" in CLAUDE.md, human 2026-10-19).

Two kinds, found by pattern (a heuristic, not a parser):
  - scalar: a non-const reference parameter of a scalar type (`int &count`, `std::size_t &next`,
    `bool &`, `float &`, `double &`, `qsizetype &`, fixed-width integers);
  - container: a `void` free function (defined in a .cpp without `Class::`) taking a non-const
    reference to a container (`QList<...> &`, `std::vector<...> &`, `QByteArray &`, ...) that it
    presumably fills; methods count as rank (2) of the rule and are not listed.
Signatures that Qt or the standard library prescribe (overrides, `operator>>`, `requestImage`) and
swap-like functions are skipped.
A declaration and its definition count once (same file stem, function and parameter name).
Warning only: prints the findings and the counts, the exit code is always 0.
Usage: scripts/check-out-params.py [--list]   (--list prints every finding, otherwise only counts per folder)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SCALARS = (r"(?:int|unsigned|long|short|bool|float|double|qsizetype|std::size_t|size_t|qint\d+|quint\d+"
           r"|std::u?int\d+_t|u?int\d+_t|char)")
SCALAR_PARAMETER = re.compile(r"(?<![\w:])(?<!const )" + SCALARS + r"\s*&\s*(\w+)\s*[,)]")
CONTAINERS = (r"(?:QList|QVector|std::vector|std::deque|std::array|QHash|QMap|QSet|std::map|std::unordered_map"
              r"|std::set|std::unordered_set|QByteArray|QString|QStringList)")
CONTAINER_PARAMETER = re.compile(r"(?<![\w:])(?<!const )" + CONTAINERS + r"(?:<[^()]*?>)?\s*&\s*(\w+)\s*[,)]")
# Function names whose signature a framework prescribes, or swap-like functions.
EXEMPT_NAMES = re.compile(r"^(?:operator.*|requestImage|requestPixmap|requestTexture|swap|qSwap)$")
FUNCTION = re.compile(r"(?:^|[;{}\n])\s*(?:static\s+|inline\s+|virtual\s+)*(\w[\w:<>,\s*&]*?)\s+([\w:~]+)\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)")


def source_files():
    for directory, _, names in os.walk(os.path.join(ROOT, "src")):
        for name in sorted(names):
            if name.endswith((".h", ".cpp", ".mm")):
                yield os.path.join(directory, name)


def findings(path):
    """(kind, key, location) of every output parameter in one file."""
    with open(path, encoding="utf-8", errors="replace") as file:
        text = file.read()

    stem = os.path.splitext(os.path.relpath(path, ROOT))[0]
    for match in FUNCTION.finditer(text):
        result_type, name, parameters = match.group(1), match.group(2), match.group(3)
        if name in ("if", "for", "while", "switch", "return", "catch", "sizeof"):
            continue
        line = text.count("\n", 0, match.start(2)) + 1
        location = f"{os.path.relpath(path, ROOT)}:{line} {name}"
        qualified = "::" in name
        name = name.split("::")[-1]
        tail = text[match.end():match.end() + 80]
        if EXEMPT_NAMES.match(name) or re.match(r"\s*(?:const\s*)?(?:noexcept\s*)?(?:override|final)\b", tail):
            continue
        for parameter in SCALAR_PARAMETER.finditer(parameters + ")"):
            yield "scalar", (stem, name, parameter.group(1)), f"{location}(… {parameter.group(0).rstrip(',)')} …)"
        if result_type.split()[-1] == "void" and path.endswith((".cpp", ".mm")) and not qualified:
            for parameter in CONTAINER_PARAMETER.finditer(parameters + ")"):
                yield "container", (stem, name, parameter.group(1)), f"{location}(… {parameter.group(0).rstrip(',)')} …)"


def main():
    show = "--list" in sys.argv[1:]
    seen = set()
    counts = {"scalar": 0, "container": 0}
    folders = {}
    for path in source_files():
        for kind, key, location in findings(path):
            if (kind, key) in seen:
                continue
            seen.add((kind, key))
            counts[kind] += 1
            folder = os.path.dirname(os.path.relpath(path, ROOT))
            folders[folder] = folders.get(folder, 0) + 1
            if show:
                print(f"{kind}: {location}")

    if not show:
        for folder, count in sorted(folders.items(), key=lambda item: -item[1]):
            print(f"{count:4d}  {folder}")
    print(f"check-out-params (warning only): {counts['scalar']} non-const scalar reference parameter(s), "
          f"{counts['container']} void function(s) filling a container parameter")
    return 0


if __name__ == "__main__":
    sys.exit(main())
