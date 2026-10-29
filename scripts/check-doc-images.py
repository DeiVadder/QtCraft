#!/usr/bin/env python3
"""Finds image references in the tracked Markdown files that name no image in docs/screenshots.

A reference is a name ending in .png or .jpg, as written in the docs: plain
(`79-25-driver-side.jpg`), with brace sets (`79-10-{hotbar,water}-{before,after}.png`, expanded),
with a wildcard (`45-1a-*`, also `…-north.png`, where … stands for the start of the name before it,
and `<name>` placeholders, both read as *). A reference counts as missing when it matches no file in
docs/screenshots now.

Without --base it lists every missing reference. --no-wildcards leaves out references whose only
pattern is a plain * (`45-1a-*`): such a name stands for "the images of a package", not for given
files, and may outlive them. With --base <rev> (for example dev) it lists only
the references that matched a file in that commit's docs/screenshots and match none now: what a
cleanup of docs/screenshots would break (decision E-933).

Usage: scripts/check-doc-images.py [--base <rev>] [--no-wildcards]   (exit code 0: nothing missing, 1: findings)
       scripts/check-doc-images.py --self-test
"""

import fnmatch
import itertools
import re
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
SCREENSHOTS = "docs/screenshots/"

# A name of an image: letters, digits and - _ . / plus the pattern characters { } , * … < >.
REFERENCE = re.compile(r"[\w./{},*…<>-]*[\w}*…>]\.(?:png|jpg)\b")


def expand_braces(text):
    """Every combination of the brace sets of `text` ({a,b}), in order."""
    parts = re.split(r"(\{[^{}]*\})", text)
    choices = [part[1:-1].split(",") if part.startswith("{") and part.endswith("}") else [part] for part in parts]
    return ["".join(combination) for combination in itertools.product(*choices)]


def patterns(reference):
    """The glob patterns of a reference's file name: braces expanded, … and <name> read as *."""
    result = []
    for name in expand_braces(reference):
        name = name.rsplit("/", 1)[-1]
        name = re.sub(r"<[^<>]*>", "*", name).replace("…", "*")
        result.append(name)
    return result


def references(text):
    """The image references of a document, in order."""
    return [match.group(0) for match in REFERENCE.finditer(text)]


def screenshot_names(rev=None):
    """The file names in docs/screenshots (and its subfolders), in the index or in the commit `rev`."""
    command = ["git", "ls-tree", "-r", "--name-only", rev, SCREENSHOTS] if rev else ["git", "ls-files", SCREENSHOTS]
    output = subprocess.run(command, cwd=PROJECT_DIR, capture_output=True, text=True, check=True).stdout
    return {line.rsplit("/", 1)[-1] for line in output.splitlines() if line}


def matches(pattern, names):
    return any(fnmatch.fnmatchcase(name, pattern) for name in names)


def plain_wildcard(reference):
    """True when the reference's only pattern is *: no brace set, no … and no <name>."""
    return "*" in reference and not any(mark in reference for mark in "{…<")


def findings(documents, names, base_names=None, wildcards=True):
    """(document, reference, pattern) for every pattern that matches no name (and matched one in base)."""
    result = []
    for document, text in documents:
        for reference in references(text):
            if not wildcards and plain_wildcard(reference):
                continue
            for pattern in patterns(reference):
                if matches(pattern, names):
                    continue
                if base_names is not None and not matches(pattern, base_names):
                    continue
                result.append((document, reference, pattern))
    return result


def tracked_documents():
    output = subprocess.run(["git", "ls-files", "*.md"], cwd=PROJECT_DIR, capture_output=True, text=True, check=True).stdout
    for path in output.splitlines():
        if path.startswith(SCREENSHOTS):
            continue
        file = PROJECT_DIR / path
        if file.is_file():
            yield path, file.read_text(encoding="utf-8", errors="replace")


def self_test():
    assert expand_braces("a-{x,y}-{1,2}.png") == ["a-x-1.png", "a-x-2.png", "a-y-1.png", "a-y-2.png"]
    assert patterns("docs/screenshots/79-27-{before,after}-tick{0,7}.png")[-1] == "79-27-after-tick7.png"
    assert patterns("…-north.png") == ["*-north.png"]
    assert patterns("79-14a-<wesen>-head-{before,after}.png") == ["79-14a-*-head-before.png", "79-14a-*-head-after.png"]
    text = "Bilder `docs/screenshots/45-1b-{cave,forest}-classic.png`, `a.jpg` und `…-north.png`, Text."
    assert references(text) == ["docs/screenshots/45-1b-{cave,forest}-classic.png", "a.jpg", "…-north.png"]
    names = {"45-1b-cave-classic.png", "x-north.jpg"}
    found = findings([("d.md", text)], names)
    assert [pattern for _, _, pattern in found] == ["45-1b-forest-classic.png", "a.jpg", "*-north.png"], found
    base = {"45-1b-forest-classic.png", "x-north.png"}
    found = findings([("d.md", text)], names, base)
    assert [pattern for _, _, pattern in found] == ["45-1b-forest-classic.png", "*-north.png"], found
    assert findings([("d.md", "`45-1a-*.png`")], set()) and not findings([("d.md", "`45-1a-*.png`")], set(), wildcards=False)
    print("self-test passed")
    return 0


def main(arguments):
    if arguments == ["--self-test"]:
        return self_test()
    wildcards = "--no-wildcards" not in arguments
    arguments = [argument for argument in arguments if argument != "--no-wildcards"]
    base = None
    if arguments:
        if len(arguments) != 2 or arguments[0] != "--base":
            print(__doc__.strip().splitlines()[-2].strip(), file=sys.stderr)
            return 2
        base = arguments[1]
    base_names = screenshot_names(base) if base else None
    found = findings(tracked_documents(), screenshot_names(), base_names, wildcards)
    for document, reference, pattern in found:
        print(f"{document}: {reference} -> no image {pattern}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
