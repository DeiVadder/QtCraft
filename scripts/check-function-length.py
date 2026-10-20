#!/usr/bin/env python3
"""Checks the function length limit in code lines (E-760): a function body may hold at most 60 code lines.

Code lines as in scripts/qc_code_lines.py: blank lines and comments (doc comments included) do not count, so
documenting a function never pushes it over the limit. clang-tidy's readability-function-size counts every
line and has no such option, so its LineThreshold is off in .clang-tidy; parameters, nesting and cognitive
complexity stay with clang-tidy.
Found by brace matching on the source with comments, literals and preprocessor lines blanked out (a
heuristic, not a parser): at namespace, class or `extern "C"` level, a `{` whose declaration has a parameter
list is a function body (free functions, methods, inline methods in headers, constructors after their
initialiser list, Objective-C++ methods); lambdas and local classes count towards the function around them.
Brace initialisers (`int x{0};`, `std::array<T, f(n)> x = {...}`, `enum {...}`, members in an initialiser
list) are no bodies; a lambda assigned at namespace level (`auto f = [](int x) {`) counts as a function.
A body counts its code lines between the braces, plus code on the line of the `{` after it and on the line of
the `}` before it. A function may opt out only with a comment `NOLINT(qc-function-length): <reason>` on a
line of its declaration or the line above it.
Checked: .cpp, .mm and .h below src/ and tests/ (tracked and new files), or the given files; QML functions
are not checked.
Usage: scripts/check-function-length.py [--self-test] [--list <n>] [file ...]
  --self-test  checks samples (comments, literals, initialisers, opt-out) and fails if one is judged wrong
  --list <n>   prints every function with more than n code lines instead of checking the limit
Exit code 0: within the limit, 1: findings.
"""

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qc_code_lines  # noqa: E402

FUNCTION_LINE_LIMIT = 60
OPT_OUT = "NOLINT(qc-function-length): "
PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECKED_SUFFIXES = (".cpp", ".mm", ".h")
STRUCTURE = re.compile(r"[{};]")
PREPROCESSOR_LINE = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.MULTILINE)
CONTAINER_KEYWORD = re.compile(r"\b(namespace|class|struct|union)\b")
ENUM_KEYWORD = re.compile(r"\benum\b")
EXTERN_BLOCK = re.compile(r"\s*(extern\s*\"x*\"\s*)?")
SINGLE_COLON = re.compile(r"(?<!:):(?!:)")
CONDITIONAL = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b")
BRACE_GROUP = re.compile(r"\{[^{}]*\}")
# A lambda introducer right before the `{`: after `=`, `(`, `,`, `{` or `return` (not an array size).
LAMBDA_INTRODUCER = re.compile(r"(?:^|[=(,{]|\breturn)\s*\[[^\[\]]*\]\s*(?:mutable\s*)?$")
# An Objective-C++ method head (`- (void)run:(int)times`) on the last line of a declaration head.
OBJC_METHOD = re.compile(r"(?:^|\n)[ \t]*[-+][ \t]*\([^()]*\)[ \t]*(\w+)[^\n]*$")
NAME_BEFORE_PAREN = re.compile(r"(operator\s*\S+?|~?[A-Za-z_][\w:~]*(?:<[^()]*>)?)\s*$")
MACRO_NAME = re.compile(r"(Q_[A-Z_]+|[A-Z][A-Z0-9_]{2,})$")


def first_branches_only(masked):
    """The masked text with the `#elif` and `#else` branches of conditional blocks blanked: branches
    that each open the same function would otherwise unbalance the braces."""
    lines = masked.split("\n")
    skipping = []
    for index, line in enumerate(lines):
        directive = CONDITIONAL.match(line)
        if directive and directive.group(1) in ("if", "ifdef", "ifndef"):
            skipping.append(False)
        elif directive and directive.group(1) in ("elif", "else") and skipping:
            skipping[-1] = True
        elif directive and directive.group(1) == "endif" and skipping:
            skipping.pop()
        elif any(skipping):
            lines[index] = qc_code_lines.blank(line)
    return "\n".join(lines)


def structure_text(masked):
    """The masked text (see qc_code_lines.masked) with only the first branch of conditional blocks and
    preprocessor lines blanked too (`#define X {`)."""
    return PREPROCESSOR_LINE.sub(lambda match: qc_code_lines.blank(match.group()), first_branches_only(masked))


def without_brace_groups(head):
    """A declaration head without the brace groups of member initialisers (`m{a = b}` is no `=`)."""
    previous = None
    while previous != head:
        previous = head
        head = BRACE_GROUP.sub("", head)
    return head


def brace_pairs(text):
    """The position of the matching `}` for every `{` of the text (an unmatched `{` closes at the end)."""
    pairs = {}
    open_braces = []
    for match in re.finditer(r"[{}]", text):
        if match.group() == "{":
            open_braces.append(match.start())
        elif open_braces:
            pairs[open_braces.pop()] = match.start()
    for position in open_braces:
        pairs[position] = len(text)
    return pairs


def paren_groups(head):
    """The (open, close) positions of the parenthesis groups at depth 0 of a declaration head."""
    groups = []
    depth = 0
    start = 0
    for index, char in enumerate(head):
        if char == "(":
            if depth == 0:
                start = index
            depth += 1
        elif char == ")" and depth > 0:
            depth -= 1
            if depth == 0:
                groups.append((start, index))
    return groups


def parameter_group(head, groups):
    """The parameter list of the declaration: the first group not after a macro name or `operator`."""
    for open_paren, close_paren in groups:
        before = head[:open_paren].rstrip()
        if before.endswith("operator"):
            continue
        name = re.search(r"[A-Za-z_]\w*$", before)
        if name and MACRO_NAME.fullmatch(name.group()):
            continue
        return open_paren, close_paren
    return groups[0]


def function_name(head, groups):
    """The name of the declared function, for the report."""
    if LAMBDA_INTRODUCER.search(head.rstrip()):
        return "lambda"
    if method := OBJC_METHOD.search(head.rstrip()):
        return method.group(1)
    if not groups:
        return "?"
    open_paren, _ = parameter_group(head, groups)
    match = NAME_BEFORE_PAREN.search(head[:open_paren])
    return match.group(1) if match else "?"


def classify(head):
    """What the `{` after a declaration head opens: container, function, member-init or initialiser."""
    if LAMBDA_INTRODUCER.search(head.rstrip()):
        return "function"
    full_head = head
    head = without_brace_groups(head)
    groups = paren_groups(head)
    tail = head[groups[-1][1] + 1:] if groups else head
    if ENUM_KEYWORD.search(tail):
        return "initialiser"
    if CONTAINER_KEYWORD.search(tail):
        return "container"
    if not groups:
        return "container" if EXTERN_BLOCK.fullmatch(head) else "initialiser"
    if "=" in tail.replace("==", "").replace("!=", "").replace("<=", "").replace(">=", ""):
        return "initialiser"

    # An Objective-C++ method head has colons between its parameters.
    if OBJC_METHOD.search(head.rstrip()):
        return "function"
    after_parameters = head[parameter_group(head, groups)[1] + 1:]
    before_brace = full_head.rstrip()[-1:]
    if SINGLE_COLON.search(after_parameters) and (before_brace.isalnum() or before_brace in "_>"):
        return "member-init"
    return "function"


def body_code_lines(flags, open_line, close_line, text_lines, open_column, close_column):
    """The code lines of a body: lines between the braces plus code after `{` and before `}`."""
    count = sum(flags[open_line + 1:close_line])
    if close_line == open_line:
        return 1 if text_lines[open_line][open_column + 1:close_column].strip() else 0
    if text_lines[open_line][open_column + 1:].strip():
        count += 1
    if text_lines[close_line][:close_column].strip():
        count += 1
    return count


class SourceFile:
    """One source with its masked text, line starts and code line flags."""

    def __init__(self, text, path):
        self.text = text
        self.path = path
        masked = qc_code_lines.masked(text, path)
        self.structure = structure_text(masked)
        self.flags = qc_code_lines.code_line_flags(masked)
        self.masked_lines = self.structure.split("\n")
        self.lines = text.split("\n")
        self.line_starts = [0]
        for line in self.lines[:-1]:
            self.line_starts.append(self.line_starts[-1] + len(line) + 1)

    def line_of(self, position):
        """The 0-based line and column of a position."""
        low, high = 0, len(self.line_starts) - 1
        while low < high:
            middle = (low + high + 1) // 2
            if self.line_starts[middle] <= position:
                low = middle
            else:
                high = middle - 1
        return low, position - self.line_starts[low]

    def opted_out(self, head_start, open_position):
        """True when the declaration or the line above it carries the opt-out comment."""
        stripped = len(self.structure[head_start:open_position]) - len(self.structure[head_start:open_position].lstrip())
        first_line, _ = self.line_of(head_start + stripped)
        open_line, _ = self.line_of(open_position)
        return any(OPT_OUT in self.lines[line] for line in range(max(first_line - 1, 0), open_line + 1))

    def function(self, head, head_start, open_position, close_position):
        """The (line, name, code lines, all lines, opted out) of the function whose body opens at `open_position`."""
        open_line, open_column = self.line_of(open_position)
        close_line, close_column = self.line_of(min(close_position, len(self.text) - 1))
        count = body_code_lines(self.flags, open_line, close_line, self.masked_lines, open_column, close_column)
        name = function_name(head, paren_groups(head))
        return open_line + 1, name, count, close_line - open_line + 1, self.opted_out(head_start, open_position)

    def functions(self):
        """Every function body found at namespace or class level."""
        pairs = brace_pairs(self.structure)
        found = []
        head_start = 0
        position = 0
        while True:
            match = STRUCTURE.search(self.structure, position)
            if match is None:
                return found
            position = match.end()
            if match.group() != "{":
                head_start = position
                continue

            head = self.structure[head_start:match.start()]
            kind = classify(head)
            if kind == "container":
                head_start = position
                continue
            close = pairs[match.start()]
            if kind == "function":
                found.append(self.function(head, head_start, match.start(), close))
                head_start = close + 1
            position = close + 1


def findings(text, path, limit):
    """The report lines of the functions of a source above the limit (and not opted out)."""
    report = []
    for line, name, count, lines, opted_out in SourceFile(text, path).functions():
        if count > limit and not opted_out:
            report.append(f"{path}:{line}: function '{name}' has {count} code lines (limit {limit}; {lines} lines with braces, comments and blank lines)")
    return report


def project_files():
    """The tracked and new sources below src/ and tests/."""
    listed = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "src", "tests"],
        cwd=PROJECT_DIR, capture_output=True, text=True, check=False).stdout.split()
    return sorted({os.path.join(PROJECT_DIR, path) for path in listed if path.endswith(CHECKED_SUFFIXES)})




def sample_lines(count):
    """`count` code lines of a sample body."""
    return "\n".join(f"    value += {index};" for index in range(count))


SELF_TEST_BODY = sample_lines(61)
SELF_TEST_COMMENTS = "\n".join(f"    // note {index}\n\n    /* block {{\n       }} */" for index in range(40))

# Samples of the self-test: (name, source, expected function names over the limit).
SELF_TEST_CASES = [
    ("61 code lines", f"int f(int value)\n{{\n{SELF_TEST_BODY}\n    return value;\n}}\n", ["f"]),
    ("60 code lines", f"int f(int value)\n{{\n{sample_lines(60)}\n}}\n", []),
    ("comments and blank lines", f"int f(int value)\n{{\n{sample_lines(60)}\n{SELF_TEST_COMMENTS}\n}}\n", []),
    ("braces in literals", f"int f(int value)\n{{\n    auto s = \"{{\";\n    char c = '{{';\n"
     f"    auto r = R\"x(}} // )\" {{)x\";\n{sample_lines(58)}\n}}\nint g() {{ return 1'000; }}\n", ["f"]),
    ("class with inline method", f"namespace n {{\nclass C\n{{\n    Q_PROPERTY(int v READ v)\npublic:\n"
     f"    int v() const\n    {{\n{SELF_TEST_BODY}\n    }}\n    int m{{0}};\n}};\n}}\n", ["v"]),
    ("constructor with brace member init", f"C::C(int a)\n    : m{{a}}, n(a)\n{{\n{SELF_TEST_BODY}\n}}\n", ["C::C"]),
    ("initialiser is no body", "static const int table[] = {\n" + ",\n".join(str(i) for i in range(80))
     + "\n};\nstd::array<int, size(E::Count)> other = {{\n" + ",\n".join(str(i) for i in range(80))
     + "\n}};\nenum class E {\n" + ",\n".join(f"V{i}" for i in range(80)) + "\n};\n", []),
    ("opt-out", f"// NOLINT(qc-function-length): generated table\nint f(int value)\n{{\n{SELF_TEST_BODY}\n}}\n", []),
    ("macro with brace", f"#define BLOCK {{\nint f(int value)\n{{\n{SELF_TEST_BODY}\n}}\n", ["f"]),
    ("lambda without parameters", f"static const int table = [] {{\n    int value = 0;\n{SELF_TEST_BODY}\n    return value;\n}}();\n",
     ["lambda"]),
    ("constructor with = in a member initialiser", f"C::C(int a)\n    : m{{a == 1}}, n{{.x = a}}\n{{\n{SELF_TEST_BODY}\n}}\n", ["C::C"]),
    ("conditional function heads", f"#if defined(A)\nint f(int value)\n{{\n#else\nint f(int value)\n{{\n#endif\n{SELF_TEST_BODY}\n}}\n"
     f"int g(int value)\n{{\n{SELF_TEST_BODY}\n}}\n", ["f", "g"]),
    ("comment continued by a backslash", f"int f(int value)\n{{\n{sample_lines(60)}\n    // note \\\n    value += 1;\n}}\n", []),
    ("Objective-C++ method", f"@implementation Q\n- (void)run:(int)times with:(int)value\n{{\n{SELF_TEST_BODY}\n}}\n@end\n", ["run"]),
]


def self_test():
    """Checks the samples; returns the exit code."""
    failures = []
    for name, source, expected in SELF_TEST_CASES:
        over = [found[1] for found in SourceFile(source, "sample.cpp").functions()
                if found[2] > FUNCTION_LINE_LIMIT and not found[4]]
        if over != expected:
            failures.append(f"{name}: expected {expected}, found {over}")
    for failure in failures:
        print(f"check-function-length self-test: {failure}")
    if not failures:
        print(f"check-function-length self-test: {len(SELF_TEST_CASES)} samples judged correctly")
    return 1 if failures else 0


def main(arguments):
    if "--self-test" in arguments:
        return self_test()
    limit = FUNCTION_LINE_LIMIT
    list_mode = "--list" in arguments
    if list_mode:
        index = arguments.index("--list")
        limit = int(arguments[index + 1])
        arguments = arguments[:index] + arguments[index + 2:]
    paths = [path for path in arguments if path.endswith(CHECKED_SUFFIXES)] if arguments else project_files()

    report = []
    for path in paths:
        if os.path.isfile(path):
            report += findings(qc_code_lines.read_source(path), path, limit)
    for line in report:
        print(line)
    return 0 if list_mode or not report else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
