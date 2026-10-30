#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Generates the API reference with QDoc (decision E-939): CMake target `docs`, Pages workflow.

The headers and QML files carry QDoc comments (`/*! ... */`) without topic commands; QDoc reads
comments only from sources, never from headers, and binds a comment without a topic only to a
function definition. So this script stages them first, below <stage>:
  headers/<path>.qdoc   every QDoc comment of the public API of src/<path> with its topic command
                        derived from the declaration below it (scripts/qc_doc_comments.py): \\class,
                        \\struct, \\union, \\namespace (with \\inmodule from the library of the folder
                        and \\inheaderfile), \\enum (with a \\value line per enumerator comment),
                        \\fn with the signature (members of class templates with their template
                        clause), \\variable (also for the variables right below a comment with the
                        marker \\qcshared, which is removed like \\qcfields), \\property,
                        \\typealias, \\typedef; an override without a comment gets \\reimp; a
                        declaration at global scope \\relates the header's first documented class,
                        else a \\headerfile page of the header; a namespace with documented members
                        but no comment of its own gets an empty \\namespace page. Comments that
                        already name a topic stay as they are.
  qml/<path>.qml        the QML files with \\qmltype and \\inqmlmodule QtCraft in the comment above
                        the root object
  lines.json            for every staged line its file and line in src/
then writes <stage>/qtcraft-build.qdocconf (includes docs/qdoc/qtcraft.qdocconf, adds sources,
header folders, output folder and the Qt modules whose index files exist) and runs qdoc with the
include paths and frameworks of the source files in compile_commands.json. <stage> is
<build>/docs/api-stage, <build> the folder of compile_commands.json. The warnings go to
<build>/docs/qdoc-warnings.txt with the places in src/ instead of the staged files (qdoc's whole
output to <build>/docs/qdoc.log); the last line of the output gives their number (the target of
stage 72: none).
Usage: scripts/api-docs.py [--qdoc <qdoc>] [--compile-commands <file>] [--out <dir>]
                           [--indexes <dir>] [--max-warnings <n>] [--only <path prefix> ...]
                           [--stage-only] [--self-test]
  --qdoc              the qdoc program (default: $QC_QDOC, else bin/qdoc of the Qt in
                      compile_commands.json, else qdoc on the PATH)
  --compile-commands  default build/debug/compile_commands.json
  --out               HTML output folder, default <folder of compile_commands.json>/docs/api
  --indexes           folder with the Qt documentation (<module>/<module>.index) for links to Qt
                      types; default $QC_QT_DOCS, else Docs/Qt-<version> two folders above the Qt
                      prefix (where the Qt installer and aqt put it); no links to Qt without it
  --max-warnings      exit code 1 when there are more warnings (counted after --only)
  --only              count and print only the warnings for files below these prefixes
  --stage-only        stage and write the configuration, do not run qdoc
  --self-test         stages samples in memory and fails on a wrong topic command
"""

import json
import os
import re
import shlex
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qc_doc_comments as docs  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QDOC_DIR = os.path.join(ROOT, "docs", "qdoc")
# The library (QDoc module) of each folder below src/.
MODULES = {"common": "qc_common", "network": "qc_network", "worldgen": "qc_worldgen", "server": "qc_server",
           "worlds": "qc_worlds", "clientcore": "qc_clientcore", "audio": "qc_audio", "app": "qc_app_core",
           "headless": "qtcraft_headless"}
QML_ROOT = os.path.join("src", "app", "qml")
QML_MODULE = "QtCraft"
# Qt modules whose index files are loaded when present (links to Qt types and QML types).
QT_INDEXES = ("qtcore", "qtgui", "qtnetwork", "qtconcurrent", "qtqml", "qtqmlmodels", "qtquick",
              "qtquickcontrols", "qtmultimedia", "qttestlib", "qtdoc")
TOPIC_COMMANDS = {"class": "class", "struct": "struct", "union": "union", "namespace": "namespace",
                  "enum": "enum", "variable": "variable", "property": "property", "typealias": "typealias",
                  "typedef": "typedef"}
SIGNATURE_DROPS = re.compile(r"\[\[[^\]]*\]\]|\b(?:Q_INVOKABLE|Q_SIGNAL|Q_SLOT|Q_REQUIRED_RESULT|virtual|static|"
                             r"explicit|inline|friend)\b\s*")
WARNING = re.compile(r"^(.*?):(\d+): \(qdoc\) (warning|error): (.*)$")


class Staged:
    """The text of a staged file and, per line, its place in the source tree."""

    def __init__(self):
        self.lines = []
        self.places = []

    def add(self, line, path, number):
        self.lines.append(line)
        self.places.append([path, number + 1])

    def text(self):
        return "\n".join(self.lines) + "\n"


# --- headers ------------------------------------------------------------------------------------

def module_of(relative):
    parts = relative.split(os.sep)
    return MODULES.get(parts[1]) if len(parts) > 2 and parts[0] == "src" else None


class Scopes:
    """What the \\fn signatures need to know about the classes of a header: the names declared
    directly in each class (types, aliases, constants: qualified in a return type) and the template
    clause of each class template (members of `QcList<T>` are `template <typename T> ... QcList<T>::f`)."""

    def __init__(self, items):
        self.names = {}
        self.templates = {}
        for item in items:
            if item.scope and item.kind in docs.TYPE_KINDS + ("typealias", "typedef", "variable"):
                self.names.setdefault(tuple(item.scope), set()).add(item.name)
            template = re.match(r"(template\s*<(.*?)>)\s*(?:class|struct|union)\b", item.text)
            if item.kind in ("class", "struct", "union") and template:
                arguments = [docs.IDENTIFIER.findall(part.split("=")[0])[-1]
                             for part in docs.split_top_level(template.group(2)) if part.strip()]
                self.templates[tuple(item.scope + [item.name])] = (template.group(1), ", ".join(arguments))

    def qualified(self, scope):
        """The class part of a member's name and the template clauses it needs."""
        parts = []
        clauses = []
        for depth in range(1, len(scope) + 1):
            template = self.templates.get(tuple(scope[:depth]))
            parts.append(scope[depth - 1] + (f"<{template[1]}>" if template else ""))
            if template:
                clauses.append(template[0] + " ")
        return "::".join(parts), "".join(clauses)


def signature(item, scopes):
    """The \\fn signature of a function item: qualified name, return types of nested types qualified."""
    head = SIGNATURE_DROPS.sub("", item.text).strip()
    head = re.sub(r"\s*(?:=\s*0|=\s*default|=\s*delete)\s*$", "", head)
    head = re.sub(r"\s+\b(?:override|final)\b", "", head)
    head = re.sub(r"\s*->\s*", " -> ", head)
    if not item.scope:
        return head
    scope, clauses = scopes.qualified(item.scope)
    template = re.match(r"(template\s*<.*?>\s*)(.*)$", head)
    prefix, head = (template.group(1), template.group(2)) if template else ("", head)
    position = name_position(head, item.name)
    if position < 0:
        return clauses + prefix + head
    return_type = qualify(head[:position], item.scope, scopes.names)
    return clauses + prefix + return_type + scope + "::" + head[position:]


def name_position(head, name):
    """Where the function name starts in its head (before the first parameter list)."""
    for match in re.finditer(re.escape(name) + r"\s*\(", head):
        before = head[:match.start()]
        if not before or not (before[-1].isalnum() or before[-1] in "_:~"):
            return match.start()
    return -1


def qualify(return_type, scope, nested):
    """`return_type` with the names of types nested in `scope` (or its parents) qualified."""
    def replace(match):
        if match.start() > 0 and return_type[max(0, match.start() - 2):match.start()] == "::":
            return match.group(0)
        for depth in range(len(scope), 0, -1):
            if match.group(0) in nested.get(tuple(scope[:depth]), ()):
                return "::".join(scope[:depth]) + "::" + match.group(0)
        return match.group(0)
    return re.sub(r"\b[A-Za-z_]\w*\b", replace, return_type)


def has_topic(comment):
    return comment is not None and docs.TOPIC.search(comment.text()) is not None


def topic_lines(item, relative, scopes, relates):
    """The topic and context commands for the comment of `item`; `relates` is the class or header
    file a declaration at global scope belongs to."""
    module = module_of(relative)
    if item.kind == "function":
        lines = ["\\fn " + signature(item, scopes)]
    else:
        lines = [f"\\{TOPIC_COMMANDS[item.kind]} {item.qualified()}"]
    if not item.scope and item.kind not in ("class", "struct", "union", "namespace"):
        lines.append("\\relates " + relates)
    if item.kind == "function":
        return lines
    if item.kind in ("class", "struct", "union", "namespace"):
        text = item.comment.text() if item.comment else ""
        if module and "\\inmodule" not in text:
            lines.append("\\inmodule " + module)
        if "\\inheaderfile" not in text:
            lines.append("\\inheaderfile " + relative[len("src/"):])
    return lines


def enum_values(item):
    """\\value lines of the documented enumerators."""
    lines = []
    for enumerator in item.enumerators:
        comment = enumerator.comment
        if comment is None or docs.is_license(comment):
            continue
        text = " ".join(" ".join(comment.lines).split())
        if comment.style in ("line", "trailing"):
            text = text.replace("\\", "\\\\")
        lines.append((f"\\value {enumerator.name} {text}".rstrip(), comment.start))
    return lines


def emit(staged, relative, item, topics, extra=()):
    """One staged comment: topics at the line of the comment, then its text, then `extra`."""
    comment = item.comment
    start = comment.start if comment else item.start
    staged.add("/*!", relative, start)
    for topic in topics:
        staged.add(topic, relative, start)
    if comment is not None:
        for offset, line in enumerate(comment.lines):
            staged.add(docs.without_markers(line), relative, comment.start + offset)
    for line, number in extra:
        staged.add(line, relative, number)
    staged.add("*/", relative, comment.end if comment else item.start)
    staged.add("", relative, comment.end if comment else item.start)


def is_documented(item):
    return item.comment is not None and item.comment.style == "qdoc" and item.is_public()


def documented_namespaces(texts):
    """The qualified names of the namespaces with a QDoc comment in any of the (path, text) pairs."""
    return {item.qualified() for relative, text in texts for item in docs.scan_header(text, relative)
            if item.kind == "namespace" and is_documented(item)}


def namespace_stubs(staged, relative, items, item, namespaces):
    """Stages a \\namespace page for each namespace around `item` that has no comment of its own,
    once per run (`namespaces` holds the documented and the stubbed ones)."""
    names = {other.qualified() for other in items if other.kind == "namespace"}
    for depth in range(1, len(item.scope) + 1):
        name = "::".join(item.scope[:depth])
        if name in names and name not in namespaces:
            namespaces.add(name)
            stub = docs.Item("namespace", name, [], "", "", item.start, item.start, "", None)
            emit(staged, relative, stub, [f"\\namespace {name}", "\\inmodule " + (module_of(relative) or "qc_common"),
                                          "\\inheaderfile " + relative[len("src/"):]])


def relates_target(staged, relative, items, namespaces):
    """What a declaration at global scope relates to: the header's first documented class, else a
    \\headerfile page of the header, staged once (its key in `namespaces`)."""
    first_class = next((other.qualified() for other in items if other.kind in ("class", "struct")
                        and not other.scope and is_documented(other)), None)
    if first_class:
        return first_class
    path = relative[len("src/"):]
    header = f"<{path}>"
    if header not in namespaces:
        namespaces.add(header)
        page = docs.Item("headerfile", header, [], "", "", 0, 0, "", None)
        # No \\title: qdoc titles the page "<path>" itself; a \\title with the path would link the
        # class name in it ("QcX .h").
        emit(staged, relative, page, [f"\\headerfile {header}", "\\inmodule " + (module_of(relative) or "qc_common"),
                                      f"\\brief Declarations at global scope in \\c {{{path}}}."])
    return header


def stage_header(text, relative, namespaces=None):
    """The staged comments of a header (Staged), or None when it has none. `namespaces` holds the
    documented namespaces of the whole tree and grows by the stubs and header pages staged."""
    namespaces = set() if namespaces is None else namespaces
    items = sorted(docs.scan_header(text, relative), key=lambda item: item.start)
    scopes = Scopes(items)
    shared = {}
    for variable, owner, confirmed in docs.shared_variables(items):
        if confirmed:
            shared.setdefault(id(owner), []).append(variable)
    staged = Staged()
    for item in items:
        public = item.is_public()
        comment = item.comment if item.comment is not None and item.comment.style == "qdoc" else None
        if comment is not None and has_topic(comment):
            continue  # staged as it is by explicit_topics()
        if public and comment is not None and (item.kind in TOPIC_COMMANDS or item.kind == "function"):
            extra = enum_values(item) if item.kind == "enum" else ()
            namespace_stubs(staged, relative, items, item, namespaces)
            needs_relates = not item.scope and item.kind not in ("class", "struct", "union", "namespace")
            relates = relates_target(staged, relative, items, namespaces) if needs_relates else None
            topics = topic_lines(item, relative, scopes, relates)
            # Variables below a \qcshared comment get their \variable in the same block.
            topics += [f"\\variable {variable.qualified()}" for variable in shared.get(id(item), [])]
            emit(staged, relative, item, topics, extra)
        elif public and item.kind == "function" and item.comment is None and not item.name.startswith("~") \
                and re.search(r"\b(override|final)\b", item.text):
            emit(staged, relative, item, ["\\fn " + signature(item, scopes), "\\reimp"])
    explicit_topics(staged, text, relative)
    return staged if staged.lines else None


def explicit_topics(staged, text, relative):
    """Stages every QDoc comment of `text` that names its topic itself, wherever it stands."""
    lines = text.split("\n")
    masked = docs.qc_code_lines.masked(text, relative).split("\n")
    for number, line in enumerate(lines):
        start = line.find("/*!")
        if start < 0 or masked[number][:start].strip() or line[:start].strip():
            continue
        end = number
        while end < len(lines) and "*/" not in lines[end][start + 3 if end == number else 0:]:
            end += 1
        comment = docs.Comment(number, end, "qdoc", docs.comment_lines(lines, "qdoc", number, end))
        if has_topic(comment):
            emit(staged, relative, docs.Item("", "", [], "", "", number, end, "", comment), [])


# --- QML ---------------------------------------------------------------------------------------

def stage_qml(text, relative):
    """The QML file with \\qmltype and \\inqmlmodule in the comment above its root object."""
    items = docs.scan_qml(text, relative)
    root = next((item for item in items if item.kind == "qmltype"), None)
    lines = text.split("\n")
    staged = Staged()
    insert = None
    if root is not None and root.comment is not None and root.comment.style == "qdoc" \
            and "\\qmltype" not in root.comment.text():
        insert = root.comment.start
    for number, line in enumerate(lines):
        if number == insert:
            indent, rest = line.split("/*!", 1)
            staged.add(indent + "/*!", relative, number)
            staged.add(f"    \\qmltype {root.name}", relative, number)
            staged.add(f"    \\inqmlmodule {QML_MODULE}", relative, number)
            staged.add(indent + "    " + rest.strip(), relative, number)
            continue
        staged.add(line, relative, number)
    return staged


# --- running qdoc ------------------------------------------------------------------------------

def stage(stage_dir):
    """Stages every header and QML file of src/; returns (header folders, QML files, line map)."""
    for sub in ("headers", "qml"):
        shutil.rmtree(os.path.join(stage_dir, sub), ignore_errors=True)
    places = {}
    header_dirs = set()
    qml_files = []
    headers = []
    for directory, dirs, files in os.walk(os.path.join(ROOT, "src")):
        for name in files:
            if name.endswith(".h"):
                with open(os.path.join(directory, name), encoding="utf-8") as f:
                    headers.append((os.path.relpath(os.path.join(directory, name), ROOT), f.read()))
    namespaces = documented_namespaces(headers)
    for directory, dirs, files in os.walk(os.path.join(ROOT, "src")):
        dirs[:] = sorted(d for d in dirs if d != "shaders")
        for name in sorted(files):
            path = os.path.join(directory, name)
            relative = os.path.relpath(path, ROOT)
            if name.endswith(".h"):
                header_dirs.add(directory)
                with open(path, encoding="utf-8") as f:
                    staged = stage_header(f.read(), relative, namespaces)
                target = os.path.join(stage_dir, "headers", relative + ".qdoc")
            elif name.endswith(".qml"):
                with open(path, encoding="utf-8") as f:
                    staged = stage_qml(f.read(), relative)
                target = os.path.join(stage_dir, "qml", relative)
                qml_files.append(target)
            else:
                continue
            if staged is None:
                continue
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "w", encoding="utf-8") as f:
                f.write(staged.text())
            places[os.path.realpath(target)] = staged.places
    with open(os.path.join(stage_dir, "lines.json"), "w", encoding="utf-8") as f:
        json.dump(places, f)
    return sorted(header_dirs), qml_files, places


def compile_flags(compile_commands):
    """qdoc options for the include paths, system includes and frameworks of the files in src/."""
    with open(compile_commands, encoding="utf-8") as f:
        entries = json.load(f)
    options = []
    for entry in entries:
        # The game's sources (src/ of whichever checkout the build belongs to), not the tests.
        if f"{os.sep}src{os.sep}" not in entry["file"] or f"{os.sep}tests{os.sep}" in entry["file"]:
            continue
        arguments = entry.get("arguments") or shlex.split(entry["command"])
        index = 0
        while index < len(arguments):
            argument = arguments[index]
            pair = None
            if argument in ("-I", "-isystem", "-iframework", "-F") and index + 1 < len(arguments):
                pair = (argument, arguments[index + 1])
                index += 1
            elif argument.startswith(("-I", "-F")) and len(argument) > 2:
                pair = (argument[:2], argument[2:])
            if pair:
                flag = {"-I": "-I", "-isystem": "-I", "-iframework": "-F", "-F": "-F"}[pair[0]]
                option = [flag, os.path.join(entry.get("directory", ""), pair[1])]
                if option not in options:
                    options.append(option)
            index += 1
    return [part for option in options for part in option]


def qt_prefix(compile_commands):
    """The Qt prefix from the -iframework/-isystem paths of compile_commands.json, or None."""
    flags = compile_flags(compile_commands)
    for value in flags:
        match = re.match(r"(.*)/(?:lib|include)(?:/Qt\w+\.framework/Headers|/QtCore)?/?$", value)
        if match and os.path.exists(os.path.join(match.group(1), "bin")):
            return match.group(1)
    return None


def find_qdoc(arguments, prefix):
    if arguments.get("--qdoc"):
        return arguments["--qdoc"]
    if os.environ.get("QC_QDOC"):
        return os.environ["QC_QDOC"]
    for candidate in ([os.path.join(prefix, "bin", "qdoc")] if prefix else []) + [shutil.which("qdoc") or ""]:
        if candidate and os.path.exists(candidate):
            return candidate
    return None


def below_any(path, prefixes):
    """True when `path` is one of `prefixes` or lies in one of them (whole path components)."""
    return any(path == prefix.rstrip("/") or path.startswith(prefix.rstrip("/") + "/") for prefix in prefixes)


def qdoc_version(qdoc):
    output = subprocess.run([qdoc, "--version"], capture_output=True, text=True).stdout
    match = re.search(r"(\d+\.\d+\.\d+)", output)
    return match.group(1) if match else None


def find_indexes(arguments, prefix, version):
    if arguments.get("--indexes"):
        return arguments["--indexes"]
    if os.environ.get("QC_QT_DOCS"):
        return os.environ["QC_QT_DOCS"]
    if prefix and version:
        candidate = os.path.normpath(os.path.join(prefix, "..", "..", "Docs", f"Qt-{version}"))
        if os.path.isdir(candidate):
            return candidate
    return None


def write_config(stage_dir, out_dir, header_dirs, qml_files, indexes):
    """The configuration qdoc runs with; returns its path and the Qt modules it links to."""
    modules = [m for m in QT_INDEXES if indexes and os.path.exists(os.path.join(indexes, m, m + ".index"))]
    lines = [
        "# Generated by scripts/api-docs.py; do not edit.",
        f"QC_QDOC_DIR = {QDOC_DIR}",
        f"include({os.path.join(QDOC_DIR, 'qtcraft.qdocconf')})",
        f"outputdir = {out_dir}",
        "headerdirs = " + " \\\n    ".join(header_dirs),
        "sourcedirs = " + os.path.join(stage_dir, "headers"),
        "sources.fileextensions = \"*.qdoc\"",
        "sources += " + " \\\n    ".join(qml_files),
    ]
    if modules:
        lines.append("indexes = " + " \\\n    ".join(os.path.join(indexes, m, m + ".index") for m in modules))
    path = os.path.join(stage_dir, "qtcraft-build.qdocconf")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return path, modules


def source_place(path, line, places):
    """The place in src/ of a line of a staged file (or the place as it is)."""
    staged = places.get(os.path.realpath(path))
    if staged and 0 < line <= len(staged):
        return staged[line - 1][0], staged[line - 1][1]
    return os.path.relpath(path, ROOT) if path.startswith(ROOT) else path, line


def map_warnings(output, places):
    """The qdoc warnings with places in src/: a list of (path, line, text with continuation lines)."""
    warnings = []
    for raw in output.splitlines():
        match = WARNING.match(raw)
        if match:
            path, line = source_place(match.group(1), int(match.group(2)), places)
            warnings.append([path, line, f"{match.group(3)}: {match.group(4)}"])
        elif raw.startswith((" ", "\t")) and warnings:
            warnings[-1][2] += "\n" + raw
    return warnings


def parse_arguments(argv):
    arguments = {"--only": []}
    index = 0
    while index < len(argv):
        name = argv[index]
        if name in ("--stage-only", "--self-test"):
            arguments[name] = True
        elif name == "--only":
            arguments["--only"].append(argv[index + 1])
            index += 1
        elif name.startswith("--") and index + 1 < len(argv):
            arguments[name] = argv[index + 1]
            index += 1
        else:
            raise SystemExit(f"unknown argument {name}\n{__doc__}")
        index += 1
    return arguments


def run(arguments):
    compile_commands = os.path.abspath(arguments.get("--compile-commands")
                                       or os.path.join(ROOT, "build", "debug", "compile_commands.json"))
    if not os.path.exists(compile_commands):
        print(f"api-docs: {compile_commands} is missing (configure a preset first)", file=sys.stderr)
        return 2
    work_dir = os.path.join(os.path.dirname(compile_commands), "docs")
    out_dir = os.path.abspath(arguments.get("--out") or os.path.join(work_dir, "api"))
    stage_dir = os.path.join(work_dir, "api-stage")
    os.makedirs(stage_dir, exist_ok=True)
    prefix = qt_prefix(compile_commands)
    qdoc = find_qdoc(arguments, prefix)
    if qdoc and not (os.path.isfile(qdoc) and os.access(qdoc, os.X_OK)):
        print(f"api-docs: {qdoc} is no program", file=sys.stderr)
        return 2
    version = qdoc_version(qdoc) if qdoc else None
    indexes = find_indexes(arguments, prefix, version)
    header_dirs, qml_files, places = stage(stage_dir)
    config, modules = write_config(stage_dir, out_dir, header_dirs, qml_files, indexes)
    print(f"api-docs: staged {len(places)} files, Qt index files: {', '.join(modules) or 'none'}")
    if arguments.get("--stage-only"):
        print(f"api-docs: configuration {config}")
        return 0
    if not qdoc:
        print("api-docs: no qdoc found (--qdoc, QC_QDOC or Qt's bin/qdoc)", file=sys.stderr)
        return 2
    if os.path.isdir(out_dir) and os.listdir(out_dir) and not os.path.exists(os.path.join(out_dir, "qtcraft.index")):
        # Only an earlier output (with its index file) is replaced, never another folder.
        print(f"api-docs: {out_dir} is not empty and holds no earlier output; not replaced", file=sys.stderr)
        return 2
    shutil.rmtree(out_dir, ignore_errors=True)
    # qdoc parses as C++20 and libc++ hides std::expected (QcResult) below C++23: the headers see 23.
    command = [qdoc, config, "-D__cplusplus=202302L"] + compile_flags(compile_commands)
    result = subprocess.run(command, capture_output=True, text=True,
                            env=dict(os.environ, QC_QDOC_DIR=QDOC_DIR))
    with open(os.path.join(work_dir, "qdoc.log"), "w", encoding="utf-8") as f:
        f.write(" ".join(shlex.quote(part) for part in command) + "\n" + result.stdout + result.stderr)
    warnings = map_warnings(result.stdout + "\n" + result.stderr, places)
    if arguments["--only"]:
        warnings = [w for w in warnings if below_any(w[0], arguments["--only"])]
    report = os.path.join(work_dir, "qdoc-warnings.txt")
    with open(report, "w", encoding="utf-8") as f:
        f.writelines(f"{path}:{line}: {text}\n" for path, line, text in warnings)
    for path, line, text in warnings[:40]:
        print(f"{path}:{line}: {text}")
    if len(warnings) > 40:
        print(f"... all in {report}")
    print(f"api-docs: {len(warnings)} qdoc warning(s); HTML in {out_dir}")
    if result.returncode != 0:
        print(f"api-docs: qdoc ended with exit code {result.returncode}", file=sys.stderr)
        return 1
    limit = arguments.get("--max-warnings")
    return 1 if limit is not None and len(warnings) > int(limit) else 0


def self_test():
    header = "\n".join([
        "#pragma once",
        "/*! \\brief A sample. */",
        "class QcSample : public QObject",
        "{",
        "    Q_OBJECT",
        "    /*! The name. */",
        "    Q_PROPERTY(QString name READ name CONSTANT)",
        "public:",
        "    struct Limits",
        "    {",
        "        /*! Bounds in bytes. \\qcshared */",
        "        int low = 0;",
        "        int high = 1;",
        "",
        "        int other = 2;",
        "        /*! Without the marker. */",
        "        int first = 0;",
        "        int second = 1;",
        "    };",
        "    /*! Kinds. */",
        "    enum class Kind {",
        "        /*! Small one. */",
        "        Small,",
        "        Big, // Big one.",
        "    };",
        "    /*! Starts with \\a enabled. */",
        "    [[nodiscard]] static Limits start(bool enabled, Kind kind = Kind::Small);",
        "    bool isSequential() const override { return true; }",
        "signals:",
        "    /*! Ready. */",
        "    void ready();",
        "private:",
        "    /*! Hidden. */",
        "    void hidden();",
        "};",
        "/*! \\fn void free() \\n Explicit. */",
        "/*! \\brief A rectangle. */",
        "struct QcRect",
        "{",
        "    /*! Adds \\a other. */",
        "    QcRect &operator+=(const QcRect &other);",
        "};",
        ""])
    staged = stage_header(header, "src/audio/QcSample.h")
    text = staged.text() if staged else ""
    wanted = ["\\class QcSample", "\\inmodule qc_audio", "\\inheaderfile audio/QcSample.h", "\\property QcSample::name",
              "\\variable QcSample::Limits::low", "\\variable QcSample::Limits::high", "\\enum QcSample::Kind",
              "\\value Small Small one.", "\\value Big Big one.",
              "\\fn QcSample::Limits QcSample::start(bool enabled, Kind kind = Kind::Small)",
              "\\fn bool QcSample::isSequential() const", "\\reimp", "\\fn void QcSample::ready()",
              "\\fn void free() \\n Explicit.", "Bounds in bytes.", "\\variable QcSample::Limits::first",
              "\\fn QcRect &QcRect::operator+=(const QcRect &other)"]
    failures = [w for w in wanted if w not in text.split("\n") and w not in text]
    for unwanted in ("Hidden.", "QcSample::Limits::other", "QcSample::Limits::second", "\\qcshared",
                     "\\headerfile"):
        if unwanted in text:
            failures.append("unwanted: " + unwanted)
    places = staged.places if staged else []
    line_of_ready = next((places[i][1] for i, line in enumerate(text.split("\n")) if line == "Ready."), None)
    if line_of_ready != 30:
        failures.append(f"place of 'Ready.': {line_of_ready} instead of 30")
    globals_header = "\n".join([
        "#pragma once",
        "/*! The faces of a block. */",
        "enum class QcSide { Up, Down };",
        "namespace QcSides {",
        "/*! Returns the opposite of \\a side. */",
        "QcSide opposite(QcSide side);",
        "}",
        ""])
    namespaces = set()
    globals_text = stage_header(globals_header, "src/common/core/QcSide.h", namespaces).text()
    for wanted_line in ("\\headerfile <common/core/QcSide.h>", "\\relates <common/core/QcSide.h>",
                        "\\namespace QcSides", "\\inmodule qc_common", "\\fn QcSide QcSides::opposite(QcSide side)"):
        if wanted_line not in globals_text.split("\n"):
            failures.append("globals: missing " + wanted_line)
    if stage_header(globals_header, "src/common/core/QcSide.h", namespaces).text().count("\\namespace") != 0:
        failures.append("globals: namespace stub staged twice")
    templates = "\n".join([
        "/*! \\brief A list. */",
        "template<typename Definition, int N = 2>",
        "class QcList",
        "{",
        "public:",
        "    /*! Removes \\a name. */",
        "    void remove(const QString &name);",
        "    /*! Reads. */",
        "    std::optional<double> (*read)(const QString &subject) = nullptr;",
        "};",
        ""])
    template_text = stage_header(templates, "src/common/core/QcList.h", set()).text()
    for wanted_line in ("\\fn template<typename Definition, int N = 2> void QcList<Definition, N>::remove(const QString &name)",
                        "\\variable QcList::read"):
        if wanted_line not in template_text.split("\n"):
            failures.append("templates: missing " + wanted_line + "\n" + template_text)
    only_namespace = "/*! \\brief Tools. */\nnamespace QcTools {\n/*! Returns one. */\nint one();\n}\n"
    if "\\headerfile" in stage_header(only_namespace, "src/common/core/QcTools.h", set()).text():
        failures.append("namespace header: a \\headerfile page without any global declaration")
    nested = "\n".join([
        "/*! \\brief Resolves. */",
        "class QcA",
        "{",
        "public:",
        "    struct Resolved;",
        "};",
        "/*! \\brief The result. */",
        "struct QcA::Resolved",
        "{",
        "    /*! The value. */",
        "    int value = 0;",
        "};",
        "/*! A result. */",
        "template<typename T>",
        "using QcResult = std::expected<T, QString>;",
        ""])
    nested_text = stage_header(nested, "src/common/core/QcA.h", set()).text().split("\n")
    for wanted_line in ("\\struct QcA::Resolved", "\\inheaderfile common/core/QcA.h", "\\variable QcA::Resolved::value",
                        "\\typealias QcResult", "\\relates QcA"):
        if wanted_line not in nested_text:
            failures.append("nested type or alias template: missing " + wanted_line + "\n" + "\n".join(nested_text))
    if any(line.startswith("\\variable QcResult") for line in nested_text):
        failures.append("alias template staged as \\variable")
    qml = "import QtQuick\n/*! \\brief A plate. */\nRectangle {\n    /*! Its text. */\n    property string text\n}\n"
    qml_text = stage_qml(qml, "src/app/qml/Plate.qml").text()
    for wanted_line in ("    \\qmltype Plate", "    \\inqmlmodule QtCraft", "    /*! Its text. */"):
        if wanted_line not in qml_text.split("\n"):
            failures.append("qml: missing " + wanted_line)
    for failure in failures:
        print("self-test: " + failure)
    if failures:
        print(text)
    print("self-test: " + ("ok" if not failures else f"{len(failures)} failure(s)"))
    return 1 if failures else 0


def main(argv):
    arguments = parse_arguments(argv)
    if arguments.get("--self-test"):
        return self_test()
    return run(arguments)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
