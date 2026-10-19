#!/usr/bin/env python3
"""Checks the test selection of scripts/verify-tests.py (decision E-405) in a throwaway git repository.

The selection compares file contents, not modification times: touching a file or switching to
another branch and back selects nothing, a changed content selects the tests that read it, and
stamps of the older mtime format count as changed once; a Mach-O executable relinked from the
same objects (new UUID, debug-map times and signature) hashes as before. ctest is not called; the
test list is injected. Usage: test_verify_tests_selection.py <path of verify-tests.py>
"""

import argparse
import contextlib
import importlib.util
import io
import os
import struct
import subprocess
import sys
import tempfile


def load_module(path):
    spec = importlib.util.spec_from_file_location("verify_tests", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def git(root, *arguments):
    command = ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false",
               *arguments]
    subprocess.run(command, cwd=root, check=True, capture_output=True)


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as file:
        file.write(text)


class Fixture:
    """A git repository with two tests: test_a reads scripts/a.sh, test_b reads scripts/b.sh."""

    def __init__(self, module, root):
        self.module = module
        self.root = root
        self.build_dir = os.path.join(root, "build", "debug")
        write(os.path.join(root, "scripts", "a.sh"), "echo a\n")
        write(os.path.join(root, "scripts", "b.sh"), "echo b\n")
        write(os.path.join(root, "content", "blocks.json"), "{}\n")
        os.makedirs(self.build_dir)
        git(root, "init", "-q", "-b", "main")
        git(root, "add", "scripts", "content")
        git(root, "commit", "-q", "-m", "base")

        module.ROOT = root
        tests = {name: {"command": ["sh", os.path.join(root, "scripts", f"{name[-1]}.sh")],
                        "properties": {}} for name in ("test_a", "test_b")}
        module.load_tests = lambda build_dir: tests

    def select(self):
        """The test names the selection prints; a passing run of them is recorded afterwards."""
        arguments = argparse.Namespace(build_dir=self.build_dir, changed=True, regex="", label="",
                                       broad_label="")
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
            self.module.select(arguments)
        regex = output.getvalue().strip()
        names = sorted(regex[2:-2].replace("\\", "").split("|")) if regex else []
        self.record(names)
        return names

    def record(self, names):
        junit = os.path.join(self.build_dir, "junit.xml")
        cases = "".join(f'<testcase name="{name}" status="run"/>' for name in names)
        write(junit, f"<testsuite>{cases}</testsuite>")
        arguments = argparse.Namespace(build_dir=self.build_dir, junit=[junit])
        with contextlib.redirect_stderr(io.StringIO()):
            self.module.record(arguments)


def check(label, actual, expected):
    if actual != expected:
        print(f"FAIL {label}: selected {actual}, expected {expected}")
        return False
    print(f"ok   {label}: {actual}")
    return True


def macho(uuid, object_time, code):
    """A minimal 64-bit Mach-O: header, LC_UUID, LC_SYMTAB with one N_OSO entry, LC_CODE_SIGNATURE."""
    header_size, uuid_size, symtab_size, signature_size = 32, 24, 24, 16
    symbols = header_size + uuid_size + symtab_size + signature_size
    signature = symbols + 16 + len(code)
    data = struct.pack("<IiiIIIII", 0xFEEDFACF, 0, 0, 2, 3, uuid_size + symtab_size + signature_size, 0, 0)
    data += struct.pack("<II16s", 0x1B, uuid_size, uuid)
    data += struct.pack("<IIIIII", 0x2, symtab_size, symbols, 1, 0, 0)
    data += struct.pack("<IIII", 0x1D, signature_size, signature, 8)
    data += struct.pack("<IBBHQ", 0, 0x66, 0, 1, object_time)
    return data + code + uuid[:8]


def check_macho(module, root):
    """Link noise of Mach-O executables does not change the content hash; code does."""
    paths = [os.path.join(root, name) for name in ("one", "two", "three")]
    for path, content in zip(paths, [macho(b"A" * 16, 1, b"code"), macho(b"B" * 16, 2, b"code"),
                                     macho(b"A" * 16, 1, b"edoc")]):
        with open(path, "wb") as file:
            file.write(content)
    hashes = [module.content_hash(path) for path in paths]
    return all([check("Mach-O relinked from the same objects", hashes[0] == hashes[1], True),
                check("Mach-O with other code", hashes[0] == hashes[2], False)])


def run_cases(fixture):
    root = fixture.root
    script_a = os.path.join(root, "scripts", "a.sh")
    results = [check("first run without stamps", fixture.select(), ["test_a", "test_b"]),
               check("unchanged", fixture.select(), [])]

    stat = os.stat(script_a)
    os.utime(script_a, ns=(stat.st_atime_ns, stat.st_mtime_ns + 5_000_000_000))
    results.append(check("touched without change", fixture.select(), []))

    git(root, "checkout", "-q", "-b", "other")
    write(script_a, "echo other\n")
    write(os.path.join(root, "content", "blocks.json"), '{"other": 1}\n')
    git(root, "commit", "-q", "-am", "other")
    git(root, "checkout", "-q", "main")
    results.append(check("other branch and back", fixture.select(), []))

    write(script_a, "echo changed\n")
    results.append(check("content of a.sh changed", fixture.select(), ["test_a"]))

    write(os.path.join(root, "content", "blocks.json"), '{"changed": 1}\n')
    results.append(check("runtime data changed", fixture.select(), ["test_a", "test_b"]))

    stamps_path = os.path.join(fixture.build_dir, fixture.module.STAMPS)
    stamps = fixture.module.read_json(stamps_path, {})
    stamps["tests"]["test_b"]["inputs"] = "old mtime format"
    fixture.module.write_json(stamps_path, stamps)
    results.append(check("old stamp format counts as changed once", fixture.select(), ["test_b"]))
    results.append(check("and then not again", fixture.select(), []))
    return all(results)


def main():
    module = load_module(sys.argv[1])
    with tempfile.TemporaryDirectory() as root:
        fixture = Fixture(module, os.path.realpath(root))
        passed = run_cases(fixture)
        passed = check_macho(module, root) and passed
        return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
