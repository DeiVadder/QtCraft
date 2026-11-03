#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Checks of the code style rules of E-379 (Qt or the standard library), decision E-948.

The rules run clang-query (Homebrew LLVM) with AST matchers over build/debug/compile_commands.json, one
process per translation unit, in parallel, with a cache per translation unit. Raw logs and logging
categories defined more than once are checked by scripts/check-logging.py (E-680, gating), not here.

Rules (name, what it finds):
  std-string        (AST) a type written as std::string (any std::basic_string) or a call returning one
                    (`toStdString()`, `std::to_string`) in src/ or tests/: strings are QString (E-379)
  delete            (AST) a delete expression written in src/ or tests/: ownership lives in the type
                    (std::unique_ptr, the QObject parent, deleteLater), E-379/E-381
  lock-type         (AST) a variable or member of type QMutex, QRecursiveMutex, QReadWriteLock or a recursive
                    std mutex, also as pointer, reference, array or template argument (`std::unique_ptr<QMutex>`;
                    lockers such as QMutexLocker excepted), and (text) a Q_GLOBAL_STATIC of such a type:
                    synchronisation uses std::mutex (ThreadSanitizer sees it), never a recursive one
  mutex-handover    (AST) a std::mutex (also timed, shared; the same variants, Q_GLOBAL_STATIC included) whose declaration says nowhere which threads hand
                    over data through it: the comment on its line or the comment block directly above it
                    must contain the word "thread" or name E-010 (thread hand-overs under std::mutex);
                    heuristic of E-948: a mutex nobody can name the threads for guards nothing that crosses
                    threads
  int-kind          (AST) a member or parameter of an integer type (not bool) named `kind`, `type`, `mode`,
                    `side` or camel case ending in Kind, Type or Mode (not `...PerKind`), parameters of
                    overrides excepted (not those of a lambda inside one): a fixed set of values is an enum class, an index into a list of
                    definitions a strong id (QcIndexId, e.g. QcCreatureKindId), E-952; local variables (a
                    byte a codec reads before its cast) are not checked
Detach checks stay with clazy (static-analysis.sh), they are not repeated here.

A finding is skipped when its line or the line above holds `// NOLINT(qc-<rule>): <reason>`; a marker of one of
these rules without a reason (or without the colon) is listed itself as `unreasoned` and fails the run. Rules with a limit in LIMITS
fail when their count exceeds it (0 since package 50b; int-kind a ratchet, E-952).

Usage: scripts/check-ast.py [--list] [--changed] [--jobs N] [--build-dir DIR] [--no-cache] [file ...]
  --list       prints every finding, otherwise only the counts per rule
  --changed    only sources changed against dev (committed, staged, unstaged, new)
  file ...     only these files (implies --list); a header is analysed through its .cpp, or through a
               translation unit that includes it or a header including it (two levels; otherwise a note)
  --jobs N     parallel clang-query processes (default QC_TEST_JOBS, else cores minus 2)
  --no-cache   ignores the cache (build-dir/.check-ast-cache.json)
  --self-test  checks samples (the AST part only when clang-query is installed)
Without clang-query the run prints "AST checks skipped (no clang-query)" and exits 0.
Run it after the build: a translation unit is parsed again when its compile command, its source text, the
clang-query version and arguments or the modification time of its object file changed. The object file is
rebuilt whenever one of its headers changes, so the cache holds only while the build is current (verify.sh
builds before the static analysis); a header edited after the last build is not seen through the cache.
Exit codes: 0 clean or warnings only, 1 a marker without a reason or a rule over its limit, 2 a translation
unit clang-query could not process or a missing compile_commands.json.
"""

import concurrent.futures
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_BRANCH = os.environ.get("QC_BASE_BRANCH", "dev")
CLANG_QUERY = os.environ.get("CLANG_QUERY", "/opt/homebrew/opt/llvm/bin/clang-query")
RULES = ("std-string", "delete", "lock-type", "mutex-handover", "int-kind")
# Highest count per rule before the run fails (exit code 1): 0 for every rule since package 50b (E-948);
# int-kind keeps the indices of block definitions (QcBlockState::type and its users, 15 findings) until they
# get a strong id of their own (E-952); the limit only goes down.
LIMITS = {rule: 0 for rule in RULES} | {"int-kind": 15}
NOLINT = re.compile(r"NOLINT\(qc-(" + "|".join(RULES) + r")\)(:?)(.*)")
SOURCE = re.compile(r"\.(cpp|h|mm)$")
GLOBAL_STATIC = re.compile(r"\bQ_GLOBAL_STATIC\s*\(\s*(QMutex|QRecursiveMutex|QReadWriteLock|std::\w*mutex)\b")
ERROR = re.compile(r"^/\S.*:\d+:\d+: (?:fatal )?error:|Error while processing|Skipping .* compile command",
                   re.MULTILINE)
MATCH = re.compile(r'^(/[^:\n]+):(\d+):\d+: note: "([\w-]+)" binds here', re.MULTILINE)

STRING = 'hasUnqualifiedDesugaredType(recordType(hasDeclaration(namedDecl(hasName("::std::basic_string")))))'
LOCKS = '"::QMutex", "::QRecursiveMutex", "::QReadWriteLock", "::std::recursive_mutex", "::std::recursive_timed_mutex"'
MUTEXES = '"::std::mutex", "::std::timed_mutex", "::std::shared_mutex", "::std::shared_timed_mutex"'
PLACE = 'isExpansionInFileMatching("/(src|tests)/")'
# Names of the int-kind rule (E-952): a member or parameter called so holds what kind of thing something is
# (`kind`, `type`, `mode`, `side`, or a camel-case name ending in Kind, Type or Mode); a count per kind
# (`bredPerKind`) is none.
KIND_NAMES = "(^|::)(kind|type|mode|side|[a-z][A-Za-z0-9]*(Kind|Type|Mode))$"
NOT_KIND_NAMES = "Per(Kind|Type|Mode)$"
INTEGER = 'qualType(isInteger(), hasCanonicalType(builtinType()), unless(hasCanonicalType(booleanType())))'
LOCKERS = ('"::QMutexLocker", "::QReadLocker", "::QWriteLocker", "::std::lock_guard", "::std::unique_lock", '
           '"::std::scoped_lock", "::std::shared_lock"')


def holding(names):
    """A type matcher: one of the record types `names`, or a pointer, reference, array or template (not a
    locker) of one. Every alternative of the anyOf is a qualType(...): mixed with plain Type matchers the
    anyOf matches nothing."""
    record_type = f"recordType(hasDeclaration(namedDecl(hasAnyName({names}))))"
    record = f"qualType(hasUnqualifiedDesugaredType({record_type}))"
    template = (f"qualType(hasUnqualifiedDesugaredType(recordType(hasDeclaration(classTemplateSpecializationDecl("
                f"unless(hasAnyName({LOCKERS})), hasAnyTemplateArgument(refersToType({record})))))))")
    array = f"qualType(hasUnqualifiedDesugaredType(arrayType(hasElementType(hasUnqualifiedDesugaredType({record_type})))))"
    return f"anyOf({record}, qualType(pointsTo({record})), qualType(references({record})), {array}, {template})"


QUERY = f"""set output diag
set bind-root false
match typeLoc(loc(qualType({STRING})), {PLACE}).bind("std-string")
match callExpr(hasType(qualType({STRING})), {PLACE}).bind("std-string")
match cxxDeleteExpr({PLACE}).bind("delete")
match declaratorDecl(unless(parmVarDecl()), unless(isImplicit()), unless(varDecl(isInitCapture())), hasType(qualType({holding(LOCKS)})), {PLACE}).bind("lock-type")
match declaratorDecl(unless(parmVarDecl()), unless(isImplicit()), unless(varDecl(isInitCapture())), hasType(qualType({holding(MUTEXES)})), {PLACE}).bind("std-mutex")
match fieldDecl(hasType({INTEGER}), matchesName("{KIND_NAMES}"), unless(matchesName("{NOT_KIND_NAMES}")), {PLACE}).bind("int-kind")
match parmVarDecl(hasType({INTEGER}), matchesName("{KIND_NAMES}"), unless(matchesName("{NOT_KIND_NAMES}")), unless(hasDeclContext(cxxMethodDecl(isOverride()))), {PLACE}).bind("int-kind")
"""
MESSAGES = {
    "std-string": "std::string: use QString (E-379)",
    "delete": "hand-written delete: ownership in the type (std::unique_ptr, parent, deleteLater), E-379",
    "lock-type": "Qt or recursive lock: use std::mutex (E-379)",
    "mutex-handover": "std::mutex without a comment naming the threads that hand over data through it (E-948)",
    "int-kind": "integer named like a kind: an enum class for a fixed set, a strong id for a registry index (E-952)",
}


def load_code_lines():
    import importlib.util
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "qc_code_lines.py")
    spec = importlib.util.spec_from_file_location("qc_code_lines", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


QC_CODE_LINES = load_code_lines()


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


def global_statics(path, text):
    """(line, bind name) of every Q_GLOBAL_STATIC of a lock type in code (the AST hides its type in Qt's
    holder)."""
    masked = QC_CODE_LINES.masked(text, path)
    found = []
    for match in GLOBAL_STATIC.finditer(masked):
        kind = match.group(1)
        recursive = kind.startswith("Q") or "recursive" in kind
        found.append((line_of(masked, match.start()), "lock-type" if recursive else "std-mutex"))
    return found


def comment_above(lines, index):
    """The trailing comment of line `index` (0-based) and the comment block directly above it."""
    parts = [lines[index].partition("//")[2]]
    above = index - 1
    while above >= 0 and lines[above].strip().startswith(("//", "/*", "*")):
        parts.append(lines[above])
        above -= 1
    return " ".join(parts)


def handover_findings(text, line):
    """The mutex-handover finding of a std mutex declared at `line` (1-based), or none."""
    lines = text.split("\n")
    comment = comment_above(lines, line - 1) if line <= len(lines) else "thread"
    if "thread" in comment.lower() or "E-010" in comment:
        return []
    return [(line, "mutex-handover", MESSAGES["mutex-handover"])]


def ast_findings(matches, texts):
    """path -> [(line, rule, message)] from clang-query matches {(path, line, bind name)}."""
    findings = {}
    for path, line, name in sorted(matches):
        if path not in texts:
            continue
        if name == "std-mutex":
            found = handover_findings(texts[path], line)
        else:
            found = [(line, name, MESSAGES[name])]
        findings.setdefault(path, []).extend(found)
    return findings


def markers(text):
    """(line, rule) -> True when the marker gives a reason."""
    found = {}
    for number, line in enumerate(text.split("\n"), start=1):
        match = NOLINT.search(line)
        if match:
            found[(number, match.group(1))] = bool(match.group(2)) and bool(match.group(3).strip())
    return found


def apply_markers(path, text, findings):
    """The findings of one file without the exempted ones, plus the markers without a reason."""
    marks = markers(text)
    reasoned = {key for key, reason in marks.items() if reason}
    kept = [(path, line, rule, message) for line, rule, message in sorted(set(findings))
            if (line, rule) not in reasoned and (line - 1, rule) not in reasoned]
    kept += [(path, line, "unreasoned", f"NOLINT(qc-{rule}): without a reason")
             for (line, rule), reason in sorted(marks.items()) if not reason]
    return kept


def git_files(*arguments):
    output = subprocess.run(["git", *arguments], cwd=ROOT, capture_output=True, text=True, check=False).stdout
    return [line for line in output.split("\n") if line]


def read_sources():
    """path below src/ or tests/ -> text, for every C++ source of the tree (tracked or new)."""
    texts = {}
    for path in git_files("ls-files", "--cached", "--others", "--exclude-standard", "src/", "tests/"):
        full = os.path.join(ROOT, path)
        if SOURCE.search(path) and os.path.isfile(full):
            with open(full, encoding="utf-8", errors="replace") as file:
                texts[path] = file.read()
    return texts


def changed_files():
    """C++ sources below src/ and tests/ changed against the base branch: committed, staged, unstaged, new."""
    files = set(git_files("diff", "--name-only", f"{BASE_BRANCH}...HEAD"))
    files |= set(git_files("diff", "--name-only", "HEAD"))
    files |= set(git_files("ls-files", "--others", "--exclude-standard"))
    return {path for path in files if path.startswith(("src/", "tests/")) and SOURCE.search(path)
            and os.path.isfile(os.path.join(ROOT, path))}


def compile_entries(build_dir):
    """path below src/ or tests/ -> compile_commands.json entry of its translation unit."""
    with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as file:
        entries = json.load(file)
    prefix = ROOT + "/"
    return {entry["file"][len(prefix):]: entry for entry in entries
            if entry["file"].startswith(prefix + "src/") or entry["file"].startswith(prefix + "tests/")}


def includers(path, texts):
    """The sources that include the header `path` directly (includes name the path below src/ or tests/,
    E-418), translation units first."""
    include = f'"{path.split("/", 1)[1]}"'
    found = [other for other in sorted(texts) if other != path and include in texts[other]]
    return sorted(found, key=lambda other: other.endswith(".h"))


def unit_for_header(path, entries, texts, depth=2):
    """A translation unit that shows the findings of the header `path`: its own .cpp, a unit including it,
    or one including a header that includes it (up to `depth` levels); None when there is none."""
    same = path[:-2] + ".cpp"
    if same in entries:
        return same
    for other in includers(path, texts):
        if other in entries:
            return other
    if depth > 1:
        for header in includers(path, texts):
            unit = header.endswith(".h") and unit_for_header(header, entries, texts, depth - 1)
            if unit:
                return unit
    return None


def units_for(selected, entries, texts):
    """The translation units that show the findings of the selected files (None: all of them)."""
    if selected is None:
        return sorted(entries)
    units = set()
    for path in sorted(selected):
        if path in entries:
            units.add(path)
            continue
        if not path.endswith(".h"):
            continue
        unit = unit_for_header(path, entries, texts)
        if unit:
            units.add(unit)
        else:
            print(f"note: {path}: no translation unit includes it within two levels, AST rules not checked",
                  file=sys.stderr)
    return sorted(units)


def query_arguments():
    sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True, check=False).stdout.strip()
    arguments = ["--extra-arg=-Wno-unknown-warning-option", "--extra-arg=-Wno-everything"]
    if sdk:
        arguments.append(f"--extra-arg=-isysroot{sdk}")
    return arguments


def run_query(source, arguments, build_dir=None, extra=()):
    """(matches {(absolute path, line, bind name)}, error text or None) of one translation unit. Errors are
    compiler errors (`/path:line:col: error:`) and tool failures, never a matched source line."""
    with tempfile.NamedTemporaryFile("w", suffix=".query", delete=False) as file:
        file.write(QUERY)
        query = file.name
    command = [CLANG_QUERY, "-f", query, *arguments]
    if build_dir:
        command += ["-p", build_dir]
    command += [source, *extra]
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    os.unlink(query)
    matches = {(match.group(1), int(match.group(2)), match.group(3)) for match in MATCH.finditer(result.stdout)}
    errors = [line for line in result.stderr.split("\n") if ERROR.search(line)]
    errors += [line for line in result.stdout.split("\n") if ERROR.search(line)]
    if result.returncode and not errors:
        errors = [f"clang-query exited with {result.returncode}"]
    return matches, ("\n".join(errors) if errors else None)


def tool_identity(arguments):
    """The clang-query binary, its version and its arguments: part of every cache key."""
    version = subprocess.run([CLANG_QUERY, "--version"], capture_output=True, text=True, check=False).stdout
    return "\n".join([CLANG_QUERY, version, *arguments])


def cache_key(entry, text, identity):
    """The cache key of a translation unit: its command, its text, the tool and the time of its object file."""
    output = os.path.join(entry["directory"], entry.get("output", ""))
    if not entry.get("output") or not os.path.isfile(output):
        return None
    digest = hashlib.sha1((QUERY + identity + entry.get("command", str(entry.get("arguments")))).encode())
    digest.update(text.encode())
    return f"{digest.hexdigest()}:{os.stat(output).st_mtime_ns}"


def load_cache(path):
    try:
        with open(path, encoding="utf-8") as file:
            return json.load(file)
    except (OSError, ValueError):
        return {}


def save_cache(path, cache):
    """Writes the cache through a unique temporary file, so parallel runs never mix their writes."""
    try:
        handle, temporary = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".check-ast-cache.")
        with os.fdopen(handle, "w", encoding="utf-8") as file:
            json.dump(cache, file)
        os.replace(temporary, path)
    except OSError:
        pass


def analyse_units(units, entries, texts, options):
    """{(path below ROOT, line, bind name)} of the units, and the units clang-query could not process."""
    cache_path = os.path.join(options["build_dir"], ".check-ast-cache.json")
    cache = {} if options["no_cache"] else load_cache(cache_path)
    arguments = query_arguments()
    identity = tool_identity(arguments)
    keys = {unit: cache_key(entries[unit], texts.get(unit, ""), identity) for unit in units}
    pending = [unit for unit in units if not keys[unit] or cache.get(unit, {}).get("key") != keys[unit]]
    failed = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=options["jobs"]) as pool:
        results = pool.map(lambda unit: (unit, run_query(entries[unit]["file"], arguments, options["build_dir"])),
                           pending)
        for unit, (matches, error) in results:
            if error:
                failed[unit] = error
                cache.pop(unit, None)
                continue
            cache[unit] = {"key": keys[unit], "matches": sorted(matches)}
    save_cache(cache_path, cache)
    prefix = ROOT + "/"
    matches = set()
    for unit in units:
        for path, line, name in cache.get(unit, {}).get("matches", []):
            if path.startswith(prefix) and not path.startswith(prefix + "build/"):
                matches.add((path[len(prefix):], line, name))
    return matches, failed, len(pending)


def options_of(arguments):
    jobs = int(os.environ.get("QC_TEST_JOBS", max(1, (os.cpu_count() or 4) - 2)))
    if "--jobs" in arguments:
        jobs = int(arguments[arguments.index("--jobs") + 1])
    build_dir = os.path.join(ROOT, "build", "debug")
    if "--build-dir" in arguments:
        build_dir = os.path.abspath(arguments[arguments.index("--build-dir") + 1])
    values = {arguments[index + 1] for index, argument in enumerate(arguments[:-1])
              if argument in ("--jobs", "--build-dir")}
    files = {os.path.relpath(os.path.abspath(path), ROOT) for path in arguments
             if not path.startswith("--") and path not in values}
    return {"jobs": jobs, "build_dir": build_dir, "no_cache": "--no-cache" in arguments, "files": files}


def report(findings, file_count, analysed, arguments):
    """Prints the findings and counts; returns the exit code."""
    if "--list" in arguments:
        for path, line, rule, message in findings:
            print(f"{path}:{line}: [{rule}] {message}")
    gated = 0
    for rule in RULES + ("unreasoned",):
        number = sum(1 for finding in findings if finding[2] == rule)
        limit = 0 if rule == "unreasoned" else LIMITS.get(rule)
        over = limit is not None and number > limit
        gated += number if over else 0
        print(f"{rule}: {number}" + (" (warning only)" if limit is None else f" (limit {limit})"))
    print(f"AST checks (E-948): {len(findings)} findings in {file_count} files, {analysed} translation units "
          f"parsed, {gated} over a limit")
    return 1 if gated else 0


def main(arguments):
    if "--self-test" in arguments:
        return self_test()
    options = options_of(arguments)
    if not os.access(CLANG_QUERY, os.X_OK):
        print(f"AST checks skipped (no clang-query at {CLANG_QUERY}; brew install llvm)")
        return 0
    if not os.path.isfile(os.path.join(options["build_dir"], "compile_commands.json")):
        print(f"compile_commands.json missing in {options['build_dir']}", file=sys.stderr)
        return 2
    selected = {path for path in options["files"] if SOURCE.search(path)} or (
        changed_files() if "--changed" in arguments else None)
    if selected is not None:
        arguments = [*arguments, "--list"]
    texts = read_sources()
    entries = compile_entries(options["build_dir"])
    matches, failed, analysed = analyse_units(units_for(selected, entries, texts), entries, texts, options)
    for path, text in texts.items():
        matches |= {(path, line, name) for line, name in global_statics(path, text)}
    per_file = ast_findings(matches, texts)
    findings = []
    for path in sorted(texts):
        if selected is None or path in selected:
            findings += apply_markers(path, texts[path], per_file.get(path, []))
    for unit, error in sorted(failed.items()):
        print(f"{unit}: clang-query could not process the file:\n{error}", file=sys.stderr)
    code = report(findings, len(texts) if selected is None else len(selected), analysed, arguments)
    return 2 if failed else code


TEXT_SAMPLE = """// SPDX
Q_GLOBAL_STATIC(QMutex, qtLock)
Q_GLOBAL_STATIC(std::mutex, unexplained)
// Shared by the worker threads.
Q_GLOBAL_STATIC(std::mutex, explained)
// Q_GLOBAL_STATIC(QMutex, inAComment)
void f(int *value)
{
    delete value; // NOLINT(qc-delete): sample exemption
    const int gap = 0;
    delete value; // NOLINT(qc-delete):
    const int other = 0;
    delete value; // NOLINT(qc-delete)
}
"""

AST_SAMPLE = """#include <memory>
#include <mutex>
#include <string>
class QMutex {};
template <typename T> class QMutexLocker { public: explicit QMutexLocker(T *) {} };
struct Holder
{
    std::string name = "error: in a literal on a matched line";
    QMutex qtLock;
    std::recursive_mutex recursive;
    // Hands the result from the worker thread to the caller.
    std::mutex guarded;
    std::mutex unexplained;
    std::mutex exempt; // NOLINT(qc-mutex-handover): sample exemption
    std::mutex referenced; // hand-over of E-010
    std::unique_ptr<QMutex> owned;
    QMutex *pointer = nullptr;
    QMutex locks[2];
    std::unique_ptr<std::mutex> ownedMutex;
};
int count(const char *text) { return static_cast<int>(std::to_string(1).size()) + (text ? 1 : 0); }
void drop(int *value) { delete value; }
struct NoCopy { NoCopy(const NoCopy &) = delete; };
void lock(Holder &holder, QMutex &parameter) { QMutexLocker<QMutex> locker(&parameter); (void)holder; }
std::mutex shared; // taken by the worker threads; the lambda's capture below is no declaration
void capture() { auto take = [&shared = shared] { const std::scoped_lock lock(shared); }; take(); }
enum class Scoped { A };
struct Base { virtual ~Base() = default; virtual void pick(int type) { (void)type; } };
struct Kinds : Base
{
    int type = 0;
    unsigned char mobType = 0;
    bool kind = false;
    Scoped mode = Scoped::A;
    int sideLength = 0;
    int bredPerKind = 0;
    void pick(int type) override { auto twice = [](int kind) { return 2 * kind; }; (void)twice(type); }
    void set(int side, int other) { const int kind = side + other; (void)kind; }
    int side = 0; // NOLINT(qc-int-kind): sample exemption
};
"""


def text_self_test():
    failures = []
    got = sorted(global_statics("src/server/A.cpp", TEXT_SAMPLE))
    if got != [(2, "lock-type"), (3, "std-mutex"), (5, "std-mutex")]:
        failures.append(f"Q_GLOBAL_STATIC of lock types in code, not in comments: {got}")
    per_file = ast_findings({("src/server/A.cpp", line, name) for line, name in got}
                            | {("src/server/A.cpp", line, "delete") for line in (9, 11, 13)},
                            {"src/server/A.cpp": TEXT_SAMPLE})
    found = apply_markers("src/server/A.cpp", TEXT_SAMPLE, per_file["src/server/A.cpp"])
    got = sorted((line, rule) for _, line, rule, _ in found)
    expected = [(2, "lock-type"), (3, "mutex-handover"), (11, "delete"), (11, "unreasoned"), (13, "delete"),
                (13, "unreasoned")]
    if got != expected:
        failures.append(f"a reason exempts, a marker without reason or colon does not: expected {expected}, "
                        f"got {got}")
    return failures


def ast_self_test():
    if not os.access(CLANG_QUERY, os.X_OK):
        print(f"AST part skipped: {CLANG_QUERY} not installed")
        return []
    with tempfile.TemporaryDirectory() as folder:
        source = os.path.join(os.path.realpath(folder), "src", "Sample.cpp")
        os.makedirs(os.path.dirname(source))
        with open(source, "w", encoding="utf-8") as file:
            file.write(AST_SAMPLE)
        matches, error = run_query(source, query_arguments(), extra=["--", "-std=c++23"])
    if error:
        return [f"clang-query failed on the sample (a literal with error: must not count): {error}"]
    per_file = ast_findings({("src/Sample.cpp", line, name) for _, line, name in matches},
                            {"src/Sample.cpp": AST_SAMPLE})
    found = apply_markers("src/Sample.cpp", AST_SAMPLE, per_file.get("src/Sample.cpp", []))
    got = sorted({(line, rule) for _, line, rule, _ in found})
    expected = [(8, "std-string"), (9, "lock-type"), (10, "lock-type"), (13, "mutex-handover"),
                (16, "lock-type"), (17, "lock-type"), (18, "lock-type"), (19, "mutex-handover"),
                (21, "std-string"), (22, "delete"), (28, "int-kind"), (31, "int-kind"), (32, "int-kind"), (37, "int-kind"),
                (38, "int-kind")]
    return [] if got == expected else [f"AST sample: expected {expected}, got {got}"]


def self_test():
    failures = text_self_test() + ast_self_test()
    for failure in failures:
        print(f"FAIL: {failure}")
    print("check-ast self-test: " + ("failed" if failures else "all samples judged correctly"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
