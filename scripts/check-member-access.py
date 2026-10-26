#!/usr/bin/env python3
"""Lists `m_` members used in more than two places without going through a getter or setter (rule "Getter und
Setter für mehrfach benutzte Member" in CLAUDE.md, human 2026-10-26; E-891).

Per class (also classes defined in a .cpp, nested classes as `Outer::Inner`) every data member whose name
starts with `m_` is counted: each occurrence in a method body of that class is one place, a write when it
assigns the member or a path below it (`m_x = `, `m_x.field += `, `m_x[i] = `, `++m_x`, `>> m_x`,
`m_x << ...` at the start of a statement) or calls a
mutating container method on it (`m_list.append(`, MUTATORS), otherwise a read. Method bodies are the inline
bodies in the class and the out-of-class definitions `Class::method(...) {` in any file below src/, lambdas
inside them included. Not counted: constructors (initialiser list and body), the destructor, the member's
own accessors, default member initialisers, and uses on another object (`other.m_x`, `p->m_x`; `this->m_x`
counts).
Accessors of `m_foo`: a getter named `foo`, `isFoo`, `hasFoo` or `getFoo` whose body reads `m_foo` and does
not write it (`open()` setting `m_open` is no getter), or any method whose body is only `return m_foo;`; a setter named `setFoo`
(`setFoo` also for `m_isFoo`) whose body writes `m_foo`.
A member with more than two places is listed:
  - missing: it is read without a getter or written without a setter ("add getter/setter");
  - bypass: the accessors exist, but the class still uses the member directly ("use the accessors").
Skipped: plain data classes (every data member public and no non-const, non-static method other than
constructors and operators: protocol messages, definitions, parameter structs), synchronisation members
(mutexes, wait conditions, semaphores), reference members (`T &m_x`, `const T &m_x`) and pointers not
reassigned after construction (`T *const m_x`, or `T *m_x` written only in constructors; human 2026-10-26,
E-891), and members marked with `// NOLINT(qc-member-access): <reason>` on
their declaration line or the comment line above it (a marker without a reason still exempts the member but
is listed itself as `unreasoned`).
Template classes are scanned like other classes (`template <class T> class X {`), out-of-class definitions
`X<T>::f` belong to `X`.
Known false positives: smart pointers (`std::unique_ptr`, `QPointer`) used like fixed links are counted;
methods of a same-named class in another file (classes are matched by name, a file with the same stem wins); calls with a side effect hidden behind a non-listed method name are
reads. Known false negatives: uses in methods whose definition the brace scanner misses (macros that open a
body), members of anonymous structs, members not named `m_`,
writes through a reference alias (`auto &x = m_x; x = 1;`), and uses from friends or other classes.
Heuristic on comment- and literal-blanked text (scripts/qc_code_lines.py), not a parser.
Warning only: prints the findings and the counts, the exit code is always 0.
Usage: scripts/check-member-access.py [--list] [--changed] [file ...]
  --list     prints every finding, otherwise only counts per folder
  --changed  only classes declared or defined in files changed against dev (committed, staged, unstaged, new)
  file ...   only classes declared or defined in these files (implies --list)
"""

import importlib.util
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_BRANCH = os.environ.get("QC_BASE_BRANCH", "dev")
NOLINT = "NOLINT(qc-member-access):"

KEYWORDS = {"if", "for", "while", "switch", "return", "catch", "sizeof", "decltype", "static_assert", "alignof",
            "alignas", "noexcept", "new", "delete", "throw", "co_return", "requires", "defined", "foreach",
            "Q_FOREACH", "forever", "emit", "Q_EMIT", "typeid", "static_cast", "reinterpret_cast", "const_cast",
            "dynamic_cast"}
MUTATORS = (r"append|prepend|push_back|push_front|emplace|emplace_back|emplace_front|insert|insertMulti|clear|remove"
            r"|removeAll|removeOne|removeAt|removeFirst|removeLast|removeIf|erase|resize|reserve|squeeze|fill|replace"
            r"|takeFirst|takeLast|takeAt|take|pop_back|pop_front|swap|unite|subtract|intersect|reset"
            r"|setValue|assign|sort|truncate|chop|emplace_hint|try_emplace|insert_or_assign|store|exchange"
            r"|fetch_add|fetch_sub|fetchAndAddRelaxed|storeRelaxed|storeRelease|start|stop|restart|invalidate|setInterval"
            r"|setSingleShot")
WRITE = r"\s*(?:=(?!=)|\+=|-=|\*=|/=|%=|\|=|&=|\^=|<<=|>>=|\+\+|--)"
PATH = r"(?:\s*\.\s*\w+|\s*\[[^\]\n]*\])*"
WRITE_AFTER = re.compile(rf"{PATH}{WRITE}|{PATH}\s*\.\s*(?:{MUTATORS})\s*\(")
SYNC_TYPES = re.compile(r"\b(?:QMutex|QRecursiveMutex|QReadWriteLock|QWaitCondition|QSemaphore|std::mutex"
                        r"|std::recursive_mutex|std::timed_mutex|std::shared_mutex|std::condition_variable\w*)\b")
CLASS = re.compile(r"(?<![\w:])(?<!enum )(?<!enum  )(class|struct)\s+(?:\w+_EXPORT\s+|Q_DECL_EXPORT\s+"
                   r"|alignas\s*\([^()]*\)\s+)?(\w+)(?:\s+final)?\s*(?::(?!:)[^;{}()]*)?\{")
FUNCTION_NAME = re.compile(r"((?:\w+(?:<[^<>;{}()]*>)?::)*)(~?\w+|operator\s*(?:\(\s*\)|[^\s\w(]+|\w+))\s*\(")
ACCESS = re.compile(r"\b(public|private|protected|signals|Q_SIGNALS)\b\s*(?:Q_SLOTS\s*|slots\s*)?:(?!:)")


def load_function_length():
    """scripts/check-function-length.py as a module, for its blanking of preprocessor lines."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "check-function-length.py")
    spec = importlib.util.spec_from_file_location("check_function_length", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


FUNCTION_LENGTH = load_function_length()


def prepared(text, path):
    """Comments, literal contents, preprocessor lines and #else branches blanked; offsets and lines kept."""
    return FUNCTION_LENGTH.structure_text(FUNCTION_LENGTH.qc_code_lines.masked(text, path))


def matching(text, start, open_char, close_char):
    """Index of the bracket closing the one at start (len(text) - 1 when unbalanced)."""
    depth = 0
    for index in range(start, len(text)):
        if text[index] == open_char:
            depth += 1
        elif text[index] == close_char:
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


def flattened(body):
    """The class body with everything inside nested (), {} and [] replaced by spaces (line breaks kept)."""
    out, depth = [], 0
    for character in body:
        if character in "({[":
            depth += 1
            out.append(character if depth == 1 else " ")
        elif character in ")}]":
            out.append(character if depth == 1 else " ")
            depth -= 1
        else:
            out.append(character if depth == 0 or character == "\n" else " ")
    return "".join(out)


def body_start(text, position):
    """Index of the `{` opening the body after a parameter list ending before position (skipping qualifiers
    and a constructor's initialiser list), or None when the signature is no definition."""
    qualifiers = re.compile(r"\s*(?:const\b|volatile\b|&&|&|noexcept\s*(?:\([^()]*\))?|override\b|final\b"
                            r"|->\s*[\w:<>,\s*&]+?(?=\s*\{)|Q_DECL_\w+|\[\[\w+\]\])")
    while True:
        match = qualifiers.match(text, position)
        if not match or match.end() == position:
            break
        position = match.end()
    rest = re.compile(r"\s*").match(text, position).end()
    if rest < len(text) and text[rest] == "{":
        return rest
    if not text.startswith(":", rest) or text.startswith("::", rest):
        return None
    index = rest + 1
    while index < len(text):
        character = text[index]
        if character == "(":
            index = matching(text, index, "(", ")")
        elif character == ";":
            return None
        elif character == "{":
            if text[:index].rstrip()[-1:] in (")", "}"):
                return index
            index = matching(text, index, "{", "}")
        index += 1
    return None


def functions(text):
    """(qualifier, name, body start, body end, signature tail) of every function definition, outermost
    first; a body is skipped after it is found, so calls and lambdas inside it are no definitions."""
    position = 0
    while True:
        match = FUNCTION_NAME.search(text, position)
        if not match:
            return
        name = match.group(2)
        close = matching(text, match.end() - 1, "(", ")")
        if name in KEYWORDS or re.match(r"Q_[A-Z_]+$", name) or text[:match.start()].rstrip().endswith((".", "->")):
            position = match.end()
            continue
        start = body_start(text, close + 1)
        if start is None:
            position = match.end()
            continue
        end = matching(text, start, "{", "}")
        yield re.sub(r"<[^<>]*>", "", match.group(1)).rstrip(":"), re.sub(r"\s+", "", name), start, end, text[close + 1:start]
        position = end + 1


class ClassInfo:
    """One class definition: qualified name, file, body span and its `m_` members."""

    def __init__(self, name, qualified, path, keyword, span):
        self.name, self.qualified, self.path, self.keyword, self.span = name, qualified, path, keyword, span
        self.members = {}
        self.plain = False


def in_template_parameters(before):
    """True when the keyword stands inside an open `template <...` (`template <class T>`), not after it."""
    match = re.search(r"\btemplate\s*(<[^;{}]*)$", before)
    return bool(match) and match.group(1).count("<") > match.group(1).count(">")


def classes(text, path):
    """Every class and struct definition in one prepared file, nested ones qualified with their parent."""
    found = []
    for match in CLASS.finditer(text):
        before = text[:match.start()]
        if re.search(r"\b(?:enum|friend)\s*$", before) or in_template_parameters(before):
            continue
        open_brace = match.end() - 1
        span = (open_brace, matching(text, open_brace, "{", "}"))
        parents = [info for info in found if info.span[0] < span[0] < info.span[1]]
        parent = max(parents, key=lambda info: info.span[0]) if parents else None
        qualified = f"{parent.qualified}::{match.group(2)}" if parent else match.group(2)
        found.append(ClassInfo(match.group(2), qualified, path, match.group(1), span))
    return found


def declarations(info, text, original_lines):
    """Fills info.members with {name: (line, exempt reason or None, type text)} and sets info.plain."""
    body = flattened(text[info.span[0] + 1:info.span[1]])
    offset = info.span[0] + 1
    labels = [(match.start(), match.group(1)) for match in ACCESS.finditer(body)]
    default = "public" if info.keyword == "struct" else "private"
    all_public, mutating_method = True, False
    for match in re.finditer(r"(?<![\w.>])(m_\w+)\s*(?=[=;:,{\[]|$)", body, re.MULTILINE):
        access = next((label for start, label in reversed(labels) if start < match.start()), default)
        statement = re.split(r"[;{}]|\b(?:public|private|protected)\s*:", body[:match.start()])[-1]
        all_public = all_public and access == "public"
        if SYNC_TYPES.search(statement) or re.search(r"\(\s*$", statement):
            continue
        line = text.count("\n", 0, offset + match.start()) + 1
        info.members.setdefault(match.group(1), (line, exempt_reason(original_lines, line), statement.strip()))
    for match in FUNCTION_NAME.finditer(body):
        name = match.group(2)
        statement = re.split(r"[;{}]|:(?!:)", body[:match.start()])[-1]
        tail = body[match.end():match.end() + 40]
        if (name in KEYWORDS or name.startswith("operator") or re.match(r"Q_[A-Z_]+$", name) or name == info.name
                or name == "~" + info.name or re.search(r"\bstatic\b", statement)):
            continue
        if not re.match(r"[^;{]*?\)\s*const\b", tail):
            mutating_method = True
    info.plain = all_public and not mutating_method


def exempt_reason(lines, line):
    """The NOLINT reason on the declaration line or the comment line above: None without a marker."""
    candidates = [lines[line - 1]] if line <= len(lines) else []
    if line >= 2 and lines[line - 2].strip().startswith("//"):
        candidates.append(lines[line - 2])
    for candidate in candidates:
        marker = candidate.find(NOLINT)
        if marker >= 0:
            return candidate[marker + len(NOLINT):].strip()
    return None


def capitalised(name):
    return name[:1].upper() + name[1:]


def getter_names(member):
    base = member[2:]
    names = {base, "is" + capitalised(base), "has" + capitalised(base), "get" + capitalised(base)}
    return names


def setter_names(member):
    base = member[2:]
    names = {"set" + capitalised(base)}
    if re.match(r"(?:is|has)[A-Z]", base):
        names.add("set" + re.sub(r"^(?:is|has)", "", base))
    return names


def uses(body, member):
    """(reads, writes) of the member in one body; uses on another object are skipped."""
    reads = writes = 0
    for match in re.finditer(rf"\b{member}\b", body):
        before = body[:match.start()].rstrip()
        if before.endswith(".") or (before.endswith("->") and not re.search(r"\bthis\s*->$", before)):
            continue
        after = body[match.end():match.end() + 200]
        statement_start = re.search(r"(?:^|[;{}])\s*(?:this\s*->\s*)?$", before)
        if (WRITE_AFTER.match(after) or re.search(r"(?:\+\+|--|>>)\s*(?:this\s*->\s*)?$", before)
                or (statement_start and re.match(r"\s*<<", after))):
            writes += 1
        else:
            reads += 1
    return reads, writes


def accessor_kind(name, body, member):
    """"getter", "setter" or None for one method and one member."""
    if re.fullmatch(rf"\{{\s*return\s+(?:this\s*->\s*)?{member}\s*;\s*\}}", body):
        return "getter"
    if name in getter_names(member) and re.search(rf"\b{member}\b", body) and uses(body, member)[1] == 0:
        return "getter"
    if name in setter_names(member) and uses(body, member)[1] > 0:
        return "setter"
    return None


def scan(sources):
    """Classes and (owner hint, name, body, path) of every method body in the given {path: text} sources."""
    all_classes, methods = [], []
    for path, text in sources.items():
        prepared_text = prepared(text, path)
        found = classes(prepared_text, path)
        lines = text.split("\n")
        for info in found:
            declarations(info, prepared_text, lines)
        all_classes.extend(found)
        for qualifier, name, start, end, _ in functions(prepared_text):
            owners = [info for info in found if info.span[0] < start < info.span[1]]
            inner = max(owners, key=lambda info: info.span[0]) if owners else None
            if qualifier:
                owner = f"{inner.qualified}::{qualifier}" if inner else qualifier
            else:
                owner = inner.qualified if inner else None
            if owner:
                methods.append((owner, name, prepared_text[start:end + 1], path))
    return all_classes, methods


def resolve(owner, path, by_name):
    """The class a method belongs to: the longest qualified-name suffix match, same file stem first."""
    parts = owner.split("::")
    for index in range(len(parts)):
        candidates = by_name.get("::".join(parts[index:]), [])
        if candidates:
            stem = os.path.splitext(path)[0]
            same = [info for info in candidates if os.path.splitext(info.path)[0] == stem or info.path == path]
            return (same or candidates)[0]
    return None


def analyse(sources, selected=None):
    """Findings (kind, path, line, class, member, reads, writes, missing) for the {path: text} sources;
    with selected (a set of paths) only classes declared or defined in one of them."""
    all_classes, methods = scan(sources)
    by_name = {}
    for info in all_classes:
        by_name.setdefault(info.qualified, []).append(info)
    bodies = {}
    for owner, name, body, path in methods:
        info = resolve(owner, path, by_name)
        if info is not None:
            bodies.setdefault(id(info), []).append((name, body, path))

    result = []
    for info in all_classes:
        own = bodies.get(id(info), [])
        touched = {info.path} | {path for _, _, path in own}
        if info.plain or not info.members or (selected is not None and not touched & selected):
            continue
        for member, (line, reason, declared) in info.members.items():
            if reason == "":
                result.append(("unreasoned", info.path, line, info.qualified, member, 0, 0, ""))
            if reason is not None:
                continue
            if not fixed_link(info, member, declared, own):
                result.extend(member_finding(info, member, line, own))
    return sorted(result, key=lambda item: (item[1], item[2]))


def fixed_link(info, member, declared, own):
    """True for a reference member and for a pointer not reassigned after construction (human 2026-10-26):
    `T &m_x`, `const T &m_x`, `T *const m_x`, and `T *m_x` that no method other than a constructor writes
    (accessors included). Smart pointers (`std::unique_ptr`, `QPointer`) are not exempt."""
    if re.search(r"(?<!&)&\s*$", declared) or re.search(r"\*\s*const\s*$", declared):
        return True
    if not re.search(r"\*\s*$", declared):
        return False
    return not any(uses(body, member)[1] for name, body, _ in own if name not in (info.name, "~" + info.name))


def member_finding(info, member, line, own):
    """The finding for one member, if any."""
    reads = writes = 0
    getter = setter = False
    for name, body, _ in own:
        if name in (info.name, "~" + info.name):
            continue
        kind = accessor_kind(name, body, member)
        getter, setter = getter or kind == "getter", setter or kind == "setter"
        if kind is None:
            body_reads, body_writes = uses(body, member)
            reads, writes = reads + body_reads, writes + body_writes
    if reads + writes <= 2:
        return []
    missing = [word for word, needed in (("getter", reads and not getter), ("setter", writes and not setter))
               if needed]
    return [("missing" if missing else "bypass", info.path, line, info.qualified, member, reads, writes,
             "/".join(missing))]


def source_files():
    for directory, _, names in os.walk(os.path.join(ROOT, "src")):
        for name in sorted(names):
            if name.endswith((".h", ".cpp", ".mm")):
                yield os.path.relpath(os.path.join(directory, name), ROOT)


def changed_files():
    """Files below src/ changed against the base branch: committed, staged, unstaged and new."""
    commands = (["git", "diff", "--name-only", f"{BASE_BRANCH}...HEAD"], ["git", "diff", "--name-only", "HEAD"],
                ["git", "ls-files", "--others", "--exclude-standard"])
    files = set()
    for command in commands:
        output = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False).stdout
        files.update(line for line in output.split("\n") if line.startswith("src/"))
    return files


def message(finding):
    kind, path, line, qualified, member, reads, writes, missing = finding
    if kind == "unreasoned":
        return f"{path}:{line}: member {member} of {qualified}: {NOLINT} without a reason"
    advice = f"add {missing} (CLAUDE.md)" if kind == "missing" else "use the getter/setter (CLAUDE.md)"
    what = "outside accessors" if kind == "missing" else "bypass the accessors"
    return f"{path}:{line}: member {member} of {qualified}: {reads} reads, {writes} writes {what}; {advice}"


def main(arguments):
    files = [os.path.relpath(os.path.abspath(path), ROOT) for path in arguments if not path.startswith("--")]
    selected = set(files) if files else (changed_files() if "--changed" in arguments else None)
    show = "--list" in arguments or selected is not None
    sources = {}
    for path in source_files():
        with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as file:
            sources[path] = file.read()

    counts = {"missing": 0, "bypass": 0, "unreasoned": 0}
    folders = {}
    for finding in analyse(sources, selected):
        counts[finding[0]] += 1
        folder = os.path.dirname(finding[1])
        folders[folder] = folders.get(folder, 0) + 1
        if show:
            print(message(finding))
    if not show:
        for folder, count in sorted(folders.items(), key=lambda item: -item[1]):
            print(f"{count:4d}  {folder}")
    print(f"check-member-access (warning only): {counts['missing']} member(s) without getter/setter, "
          f"{counts['bypass']} bypassing them, {counts['unreasoned']} {NOLINT} marker(s) without a reason")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
