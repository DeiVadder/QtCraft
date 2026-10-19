#!/usr/bin/env python3
"""Checks the logging categories of src/ (decision E-680).

Every logging category is declared in the logging header of its library and defined once in the
matching source (`src/common/core/QcLogging.cpp`, `src/server/core/QcServerLogging.cpp`, ...). The
check fails when
  - a category name ("qtcraft.server") is defined more than once under src/ or tests/,
  - a category variable (lcServer) is defined more than once,
  - Q_LOGGING_CATEGORY stands in a file under src/ whose name does not end in Logging.cpp,
  - a file under src/ logs without a category (qDebug, qInfo, qWarning or qCritical).
Usage: scripts/check-logging.py [--self-test]
  (exit code 0: fine, 1: findings, printed one per line; --self-test checks bad samples in a temporary
  folder and fails if the check misses one)
"""

import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUFFIXES = (".h", ".cpp", ".mm")
DEFINITION = re.compile(r'^\s*Q_LOGGING_CATEGORY\(\s*(\w+)\s*,\s*"([^"]+)"', re.MULTILINE)
RAW_LOG = re.compile(r"(?<![\w.:>])q(Debug|Info|Warning|Critical)\s*\(")


def source_files(root, folder):
    base = os.path.join(root, folder)
    for directory, _, names in os.walk(base):
        for name in sorted(names):
            if name.endswith(SUFFIXES):
                yield os.path.join(directory, name)


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def strip_comments(line):
    return line.split("//", 1)[0]


def check(root):
    findings = []
    names = {}
    variables = {}

    for folder in ("src", "tests"):
        for path in source_files(root, folder):
            relative = os.path.relpath(path, root)
            with open(path, encoding="utf-8") as file:
                text = file.read()

            for match in DEFINITION.finditer(text):
                where = f"{relative}:{line_of(text, match.start())}"
                variable, name = match.groups()
                names.setdefault(name, []).append(where)
                variables.setdefault(variable, []).append(where)
                if folder == "src" and not path.endswith("Logging.cpp"):
                    findings.append(f"{where}: category {name} is defined outside a *Logging.cpp; declare it in the library's logging header")

            if folder != "src":
                continue
            for number, line in enumerate(text.splitlines(), 1):
                raw = RAW_LOG.search(strip_comments(line))
                if raw:
                    findings.append(f"{relative}:{number}: q{raw.group(1)} without a category; use qC{raw.group(1)} with a category")

    for name, places in sorted(names.items()):
        if len(places) > 1:
            findings.append(f"category name {name} is defined {len(places)} times: {', '.join(places)}")
    for variable, places in sorted(variables.items()):
        if len(places) > 1:
            findings.append(f"category variable {variable} is defined {len(places)} times: {', '.join(places)}")
    return findings


def write(root, relative, text):
    path = os.path.join(root, relative)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as file:
        file.write(text)


def self_test():
    samples = {
        "twice": {
            "src/a/QcALogging.cpp": 'Q_LOGGING_CATEGORY(lcA, "qtcraft.a")\n',
            "src/b/QcBLogging.cpp": 'Q_LOGGING_CATEGORY(lcB, "qtcraft.a")\n',
        },
        "variable twice": {
            "src/a/QcALogging.cpp": 'Q_LOGGING_CATEGORY(lcA, "qtcraft.a")\n',
            "tests/test_a.cpp": 'Q_LOGGING_CATEGORY(lcA, "qtcraft.test")\n',
        },
        "outside logging file": {"src/a/QcThing.cpp": 'namespace {\nQ_LOGGING_CATEGORY(lcA, "qtcraft.a")\n}\n'},
        "raw warning": {"src/a/QcThing.cpp": 'void f()\n{\n    qWarning("no category");\n}\n'},
        "raw info stream": {"src/a/QcThing.cpp": "void f()\n{\n    qInfo().noquote() << 1;\n}\n"},
    }
    clean = {
        "src/a/QcALogging.cpp": 'Q_LOGGING_CATEGORY(lcA, "qtcraft.a")\n',
        "src/a/QcThing.cpp": 'void f()\n{\n    qCWarning(lcA) << 1; // not qWarning()\n    QDebug(&s) << 2;\n}\n',
    }

    failed = []
    for label, files in samples.items():
        with tempfile.TemporaryDirectory() as root:
            for relative, text in files.items():
                write(root, relative, text)
            if not check(root):
                failed.append(label)
    with tempfile.TemporaryDirectory() as root:
        for relative, text in clean.items():
            write(root, relative, text)
        unexpected = check(root)
        if unexpected:
            failed.append("clean sample: " + "; ".join(unexpected))

    for label in failed:
        print(f"check-logging self-test: missed {label}")
    return 1 if failed else 0


def main():
    if "--self-test" in sys.argv[1:]:
        return self_test()

    findings = check(ROOT)
    if findings:
        print(f"check-logging: {len(findings)} finding(s)")
        for finding in findings:
            print(finding)
        return 1
    print("check-logging: every category defined once, no logging without a category")
    return 0


if __name__ == "__main__":
    sys.exit(main())
