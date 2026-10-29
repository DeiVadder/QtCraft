#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Lists output parameters in src/ (rule "Keine Ausgabeparameter" in CLAUDE.md, human 2026-10-19; E-694).

Kinds, found by pattern (a heuristic, not a parser):
  - scalar: a non-const reference parameter of a scalar type (`int &count`, `std::size_t &next`, `bool &`,
    `unsigned int &`, `long long &`, fixed-width integers, ...), with or without a body;
  - container: a non-const reference to a container (`QList<...> &`, `QStringList &`, `QSet<...> &`,
    `std::vector<...> &`, `QByteArray &`, `QString &`, a `using` alias of one, ...) that the body fills: a mutating method (append,
    insert, clear, ...), an assignment or compound assignment, a statement starting with `name <<` (lists
    only), a stream opened on it (`QDataStream stream(&name)`), `std::sort(name.begin() ...`, an inserter
    (`std::back_inserter(name)`, `std::inserter(name, ...)`), `std::copy(..., name.begin())`, or a write
    through its data pointer (`name.data()[i] = ...`, `memcpy(name.data(), ...)`);
  - value: a non-const reference of any other type whose body writes it: assigns it as a whole
    (`name = ...`, `name += ...`), writes a field or an element (`name.field = ...`, `name[i] = ...`,
    `name.field[i] |= ...`, `++name.count`), calls a mutating container method on a field
    (`name.list.append(...)`), reads into it from a stream (`stream >> name.field`), or does one of these
    through a reference bound to one of its fields (`Fire &fire = block.fire; fire.burn = 2;`). Value types
    (`Qc...Pos`, `QcVec3`, `std::optional`, `...Result`, JSON types, ...) also count when only declared;
  - pointer: a non-const pointer parameter written through (`*name = ...`, `name->field = ...`,
    `name->append(...)`, `name[i] = ...`);
  - unreasoned: an `out-param:` comment without a reason (it exempts nothing).
Subjects (SUBJECTS: entities, villages and their stores) are listed only when the body replaces them as a
whole, fills them as a container or streams into them; writing their fields is an action on them.
A non-const reference to an object whose methods are only called (`QcServerWorld &world`) is not listed: that
is an object being used, not an output.
Skipped: const references, stream parameters (`QDataStream &`, `QTextStream &`, `QDebug`), signatures that
Qt or the standard library prescribe (overrides, operators, `requestImage`), swap-like functions, rule rank 3
(a function returning a reference whose body returns the changed parameter: `return name;`), and functions
marked with the comment `out-param: <reason>` (a documented exception) on the signature or in the comment
above the declaration or the definition.
Scanned: free functions, methods, function templates and out-of-class constructors (`QcX::QcX(...)`, the body
after the initialiser list). Not scanned: lambdas (they are mostly callbacks whose signature the caller
prescribes, e.g. the change function of `QcNamedList::update`) and constructors defined inside the class.
A declaration and its definition count once: the key is file stem, function name, the parameter types with
qualifiers and names removed, and the parameter position, so overloads are judged separately.
Warning only: prints the findings and the counts, the exit code is always 0.
Usage: scripts/check-out-params.py [--list]   (--list prints every finding, otherwise only counts per folder)
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SCALARS = (r"(?:(?:(?:unsigned|signed|long|short)\s+)*(?:int|long|short|char|unsigned|signed)"
           r"|bool|float|double|long\s+double|qsizetype|qreal|std::size_t|size_t|std::ptrdiff_t|ptrdiff_t"
           r"|qint\d+|quint\d+|qintptr|quintptr|std::u?int\d+_t|u?int\d+_t|char\d+_t|wchar_t|uchar|ushort|uint"
           r"|ulong)")
LISTS = r"(?:QList|QVector|QVarLengthArray|std::vector|std::deque|QStringList|QSet|std::set)"
CONTAINERS = (r"(?:QList|QVector|QVarLengthArray|std::vector|std::deque|std::array|QHash|QMap|QSet|QMultiHash"
              r"|QMultiMap|std::map|std::unordered_map|std::set|std::unordered_set|QByteArray|QString|QStringList)")
VALUES = (r"(?:Qc\w*Pos|QcVec3|QcBlockId|QcItemStack|std::optional|std::pair|std::tuple|\w*Result|QVector[234]D"
          r"|QPointF?|QSizeF?|QRectF?|QColor|QJsonObject|QJsonArray|QJsonValue|QVariant|QVariantMap|QVariantList)")
# Objects with identity that live in a server or client store (players, entities, villages, machines), and
# the stores that hold them: the game systems act on them (QcPlayerHealth::damage(client),
# QcMobBehaviour::walk(mob)). Writing their fields is an action on the subject, not a result handed back to
# the caller; rule rank 2 would move every game rule into the entity types, against the split of the systems
# by responsibility (E-694). Replacing such an object as a whole, filling it as a container or streaming into
# it is still listed. Aliases of these types (`using Client = QcServerClient;`) count as well.
SUBJECTS = re.compile(r"^(?:QcServerClient|QcCreature|QcMinecart|QcProjectile|QcItemEntity"
                      r"|QcExperienceOrb|QcEntity|QcClientEntity|QcVillage|QcVillager|QcVillagerRecord|QcArmourStand"
                      r"|QcBlockEntities::Machine|QcSpawners::Entry|QcMobManager|QcBlockEntities)$")
STREAMS = re.compile(r"^(?:QDataStream|QTextStream|QDebug|QXmlStreamWriter|QXmlStreamReader)$")
# Function names whose signature a framework prescribes, or swap-like functions.
EXEMPT_NAMES = re.compile(r"^(?:operator.*|requestImage|requestPixmap|requestTexture|swap|qSwap)$")
KEYWORDS = ("if", "for", "while", "switch", "return", "catch", "sizeof", "decltype", "static_assert", "alignof",
            "Q_ASSERT", "Q_UNUSED", "emit", "Q_EMIT", "new", "delete")
PARAMETERS = r"\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)"
FUNCTION = re.compile(r"(?:^|[;{}\n])\s*(?:static\s+|inline\s+|virtual\s+|constexpr\s+|explicit\s+|friend\s+"
                      r"|\[\[nodiscard\]\]\s+)*(\w[\w:<>,\s*&]*?)\s+([\w:~]+)\s*" + PARAMETERS)
CONSTRUCTOR = re.compile(r"(?:^|[;{}\n])\s*()((?:\w+::)*(\w+)::(\w+))\s*" + PARAMETERS)
QUALIFIERS = re.compile(r"\s*(?:const\s*)?(?:noexcept\s*)?(?:->\s*[\w:<>,\s*&]+?)?\s*(?:override|final)?\s*(\{|;|:)?")
MUTATORS = (r"append|prepend|push_back|push_front|emplace|emplace_back|emplace_front|insert|insertMulti|clear|remove"
            r"|removeAll|removeOne|removeAt|removeFirst|removeLast|removeIf|erase|resize|reserve|squeeze|fill|replace"
            r"|takeFirst|takeLast|takeAt|take|pop_back|pop_front|swap|unite|subtract|intersect|reset"
            r"|setValue|assign|sort|truncate|chop|emplace_hint")
WRITE = r"\s*(?:=(?!=)|\+=|-=|\*=|/=|%=|\|=|&=|\^=|<<=|>>=|\+\+|--)"
PATH = r"(?:\s*\.\s*\w+|\s*\[[^\]\n]*\])*"


def source_files():
    for directory, _, names in os.walk(os.path.join(ROOT, "src")):
        for name in sorted(names):
            if name.endswith((".h", ".cpp", ".mm")):
                yield os.path.join(directory, name)


def split_parameters(parameters):
    """The parameters at the top level, split at commas outside of <>, () and {}."""
    depth, current, parts = 0, "", []
    for character in parameters:
        if character in "<({":
            depth += 1
        elif character in ">)}":
            depth -= 1
        if character == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += character
    parts.append(current)
    return parts


def normalized_type(part):
    """The type of one parameter without default, name and namespace or class qualifiers."""
    declaration = re.sub(r"\s+", " ", part.split("=")[0]).strip()
    declaration = re.sub(r"\s*\b\w+$", "", declaration) if re.search(r"[\w>*&]\s*\b\w+$", declaration) else declaration
    return re.sub(r"\b\w+::", "", declaration).replace(" ", "")


def signature_key(parameters):
    return tuple(normalized_type(part) for part in split_parameters(parameters) if part.strip())


def output_candidates(parameters):
    """(position, type, name, is_pointer) of every non-const lvalue reference or non-const pointer parameter."""
    for position, part in enumerate(split_parameters(parameters)):
        declaration = re.sub(r"\s+", " ", part.split("=")[0]).strip()
        match = re.match(r"(.*?[\w>])\s*([&*])\s*(?:const\s+)?(\w+)$", declaration)
        if not match or "&&" in declaration or "**" in declaration:
            continue
        parameter_type = match.group(1).strip()
        if re.match(r"^(?:const\b|.*\bconst$)", parameter_type):
            continue
        yield position, parameter_type, match.group(3), match.group(2) == "*"


def body_start(text, position):
    """Index of the opening brace of the body after a signature ending at position (skipping an initialiser
    list), or None for a declaration."""
    qualifiers = QUALIFIERS.match(text, position)
    if not qualifiers or qualifiers.group(1) is None:
        return None
    start = qualifiers.end(1) - 1
    if qualifiers.group(1) == "{":
        return start
    if qualifiers.group(1) != ":" or text.startswith("::", start):
        return None
    depth, index = 0, start + 1
    while index < len(text):
        character = text[index]
        if character in "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == ";" and depth == 0:
            return None
        elif character == "{" and depth == 0:
            previous = text[:index].rstrip()[-1:]
            if previous in (")", "}"):
                return index
            index = closing_brace(text, index)
        index += 1
    return None


def closing_brace(text, start):
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


def body_after(text, position):
    """The brace-balanced body that follows a signature ending at position, or None for a declaration."""
    start = body_start(text, position)
    if start is None:
        return None
    return text[start:closing_brace(text, start) + 1]


def statement_start(name):
    return rf"(?:^|[;{{}}])\s*{name}"


def fills(body, name, parameter_type):
    """True when the body changes the container itself: appends, inserts, removes, clears, assigns it, or
    opens a stream on it (writing single elements of a buffer is judged by writes())."""
    name = re.escape(name)
    patterns = [rf"(?<![.\w>]){name}{WRITE}",
                rf"(?<![.\w>]){name}\s*\.\s*(?:{MUTATORS})\s*\(",
                rf"\bstd::\w+\(\s*{name}\.begin",
                rf"\bstd::(?:back_|front_)?inserter\s*\(\s*{name}\b",
                rf"\bstd::\w+\([^;]*,\s*{name}\s*\.\s*begin\s*\(",
                rf"(?<![.\w>]){name}\s*\.\s*data\s*\(\s*\)\s*\[[^\]\n]*\]{WRITE}",
                rf"\b(?:std::)?mem(?:cpy|move|set)\s*\(\s*{name}\s*\.\s*data\s*\(",
                rf"\bQ(?:Data|Text)Stream\s*\w*\s*[({{]\s*&\s*{name}\b",
                rf"\bQBuffer\s*\w*\s*[({{]\s*&\s*{name}\b"]
    if re.match(LISTS, parameter_type):
        patterns.append(statement_start(name) + r"\s*<<")
    return any(re.search(pattern, body, re.MULTILINE) for pattern in patterns)


def aliases(body, name):
    """Names of non-const references the body binds to a field or element of the parameter
    (`QcFireDefinition &fire = block.fire;`); a reference returned by a method (`state.entities()`) is an
    object the method hands out for use, not a field."""
    pattern = (rf"(?:^|[;{{}}(])\s*(?!const\b)(?:auto|[\w:]+(?:<[^;=&]*>)?)\s*&\s*(\w+)\s*=\s*{re.escape(name)}"
               r"\s*(?:\.\s*\w+\b(?!\s*\()|\[)")
    return [match.group(1) for match in re.finditer(pattern, body, re.MULTILINE)]


def writes(body, name, follow=True):
    """True when the body assigns the parameter, one of its fields or elements, changes a container field,
    or reads into it from a stream; also through a reference bound to one of its fields."""
    if follow and any(writes(body, alias, False) or fills(body, alias, "") for alias in aliases(body, name)):
        return True
    name = re.escape(name)
    patterns = (rf"(?<![.\w>]){name}{PATH}{WRITE}",
                rf"(?:\+\+|--)\s*{name}\b",
                rf">>\s*{name}\b",
                rf"(?<![.\w>]){name}(?:\s*\.\s*\w+|\s*\[[^\]\n]*\])+\s*\.\s*(?:{MUTATORS})\s*\(")
    return any(re.search(pattern, body) for pattern in patterns)


def writes_through(body, name):
    """True when the body writes through a pointer parameter."""
    name = re.escape(name)
    patterns = (rf"\*\s*{name}{WRITE}",
                rf"\(\s*\*\s*{name}\s*\){PATH}{WRITE}",
                rf"(?<![.\w>]){name}\s*->\s*\w+{PATH}{WRITE}",
                rf"(?<![.\w>]){name}\s*->\s*(?:\w+{PATH}\s*\.\s*)?(?:{MUTATORS})\s*\(",
                rf"(?<![.\w>]){name}\s*\[[^\]\n]*\]{PATH}{WRITE}")
    return any(re.search(pattern, body) for pattern in patterns)


def returns_parameter(return_type, body, name):
    """Rule rank 3: the function returns a reference and the body returns the changed parameter."""
    return return_type.rstrip().endswith("&") and re.search(rf"\breturn\s+{re.escape(name)}\s*;", body)


def exempt_reason(text, position):
    """The `out-param:` reason on the signature line or in the comment above it (with template lines between):
    None without a marker, "" for a marker without a reason."""
    line_start = text.rfind("\n", 0, position) + 1
    line_end = text.find("\n", position)
    lines = [text[line_start:line_end if line_end >= 0 else len(text)]]
    for line in reversed(text[:line_start].split("\n")[:-1]):
        stripped = line.strip()
        if not stripped.startswith(("//", "template", "[[")):
            break
        lines.append(stripped)
    for line in lines:
        marker = line.find("out-param:")
        if marker >= 0:
            return re.sub(r"^[\s/*.,;:-]+", "", line[marker + len("out-param:"):]).strip()
    return None


def container_aliases():
    """Names that `using Name = <container>...;` declares anywhere in src/ (QcTexturePackProblems)."""
    names = set()
    pattern = re.compile(r"\busing\s+(\w+)\s*=\s*" + CONTAINERS + r"\b")
    for path in source_files():
        with open(path, encoding="utf-8", errors="replace") as file:
            names.update(pattern.findall(file.read()))
    return names


def subject_aliases():
    """Names that `using Name = <subject>;` declares anywhere in src/ (`QcServer::Client`,
    `QcMachineRules::Machine`): a parameter of such a type is a subject too."""
    names = set()
    pattern = re.compile(r"\busing\s+(\w+)\s*=\s*([\w:]+)\s*;")
    for path in source_files():
        with open(path, encoding="utf-8", errors="replace") as file:
            names.update(name for name, target in pattern.findall(file.read()) if SUBJECTS.match(target))
    return names


ALIASES = None
SUBJECT_ALIASES = None


def is_subject(bare):
    global SUBJECT_ALIASES
    if SUBJECT_ALIASES is None:
        SUBJECT_ALIASES = subject_aliases()
    return SUBJECTS.match(bare) or bare in SUBJECT_ALIASES


def is_container(bare):
    global ALIASES
    if ALIASES is None:
        ALIASES = container_aliases()
    return re.match(CONTAINERS + r"(?:<.*>)?$", bare) or re.sub(r"^(?:\w+::)*", "", bare) in ALIASES


def replaces(body, name):
    """True when the body replaces a subject as a whole, fills it as a container or streams into it (field
    writes on a subject are actions on it, see SUBJECTS)."""
    escaped = re.escape(name)
    patterns = (rf"(?<![.\w>]){escaped}{WRITE}",
                rf"(?:\+\+|--)\s*{escaped}\b(?!\s*[.\[])",
                rf">>\s*{escaped}\b(?!\s*[.\[])",
                statement_start(escaped) + r"\s*<<")
    return fills(body, name, "") or any(re.search(pattern, body, re.MULTILINE) for pattern in patterns)


def subject_kind(body, name, is_pointer):
    """The kind of a subject parameter: listed only when the body replaces it as a whole."""
    if body is None:
        return None
    if is_pointer:
        return "pointer" if re.search(rf"\*\s*{re.escape(name)}{WRITE}", body) else None
    return "value" if replaces(body, name) else None


def classify(parameter_type, name, body, is_pointer):
    """The kind of output parameter, or None."""
    bare = re.sub(r"^(?:typename |struct |class )", "", re.sub(r"\s+", " ", parameter_type))
    if STREAMS.match(bare):
        return None
    if is_subject(bare):
        return subject_kind(body, name, is_pointer)
    if is_pointer:
        return "pointer" if body is not None and writes_through(body, name) else None
    if re.fullmatch(SCALARS, bare):
        return "scalar"
    if is_container(bare):
        if body is None:
            return "container"
        return "container" if fills(body, name, bare) else ("value" if writes(body, name) else None)
    if re.match(r"(?:\w+::)*" + VALUES + r"(?:<.*>)?$", bare) and body is None:
        return "value"
    return "value" if body is not None and writes(body, name) else None


def signatures(text):
    """(return type, qualified name, parameters, match) of every function and out-of-class constructor."""
    for match in FUNCTION.finditer(text):
        yield match.group(1), match.group(2), match.group(3), match
    for match in CONSTRUCTOR.finditer(text):
        if match.group(3) == match.group(4):
            yield "", match.group(2), match.group(5), match


def findings(path):
    """(kind, key, location, has_body) of every output parameter in one file."""
    with open(path, encoding="utf-8", errors="replace") as file:
        text = file.read()

    relative = os.path.relpath(path, ROOT)
    stem = os.path.splitext(relative)[0]
    for return_type, qualified, parameters, match in signatures(text):
        if qualified in KEYWORDS or (return_type and return_type.split()[-1] in ("return", "new", "delete", "else")):
            continue
        line = text.count("\n", 0, match.start(2)) + 1
        name = qualified.split("::")[-1]
        tail = text[match.end():match.end() + 80]
        if EXEMPT_NAMES.match(name) or re.match(r"\s*(?:const\s*)?(?:noexcept\s*)?(?:override|final)\b", tail):
            continue
        reason = exempt_reason(text, match.start(2))
        body = body_after(text, match.end())
        types = signature_key(parameters)
        for position, parameter_type, parameter, is_pointer in output_candidates(parameters):
            key = (stem, name, types, position)
            location = (relative, line, f"{qualified}(… {parameter_type} {'*' if is_pointer else '&'}{parameter} …)")
            if reason == "":
                yield "unreasoned", key, location, True
                continue
            if reason is not None:
                yield "exempt", key, location, body is not None
                continue
            if body is not None and returns_parameter(return_type, body, parameter):
                continue
            kind = classify(parameter_type, parameter, body, is_pointer)
            if kind:
                yield kind, key, location, body is not None


def defined_keys(path):
    """Keys of every candidate parameter with a definition in one file."""
    with open(path, encoding="utf-8", errors="replace") as file:
        text = file.read()
    stem = os.path.splitext(os.path.relpath(path, ROOT))[0]
    for _, qualified, parameters, match in signatures(text):
        if body_after(text, match.end()) is None:
            continue
        types = signature_key(parameters)
        for position, _, _, _ in output_candidates(parameters):
            yield (stem, qualified.split("::")[-1], types, position)


def collect():
    """Findings with a definition win over a bare declaration; a declaration whose definition does not
    write the parameter is dropped; an exemption covers declaration and definition of the same overload."""
    judged, declared = {}, {}
    cleared, exempt = set(), set()
    for path in source_files():
        for kind, key, location, has_body in findings(path):
            if kind == "exempt":
                exempt.add(key)
                continue
            target = judged if has_body else declared
            target.setdefault((kind, key), location)
    for path in source_files():
        cleared.update(defined_keys(path))
    result = {item: value for item, value in judged.items() if item[1] not in exempt or item[0] == "unreasoned"}
    for (kind, key), value in declared.items():
        if (kind, key) not in result and key not in cleared and key not in exempt:
            result[(kind, key)] = value
    return result


def main():
    show = "--list" in sys.argv[1:]
    counts = {"scalar": 0, "container": 0, "value": 0, "pointer": 0, "unreasoned": 0}
    folders = {}
    for (kind, _), (path, line, text) in sorted(collect().items(), key=lambda item: item[1][:2]):
        counts[kind] += 1
        folder = os.path.dirname(path)
        folders[folder] = folders.get(folder, 0) + 1
        if show:
            print(f"{kind}: {path}:{line} {text}")

    if not show:
        for folder, count in sorted(folders.items(), key=lambda item: -item[1]):
            print(f"{count:4d}  {folder}")
    print(f"check-out-params (warning only): {counts['scalar']} scalar, {counts['container']} container, "
          f"{counts['value']} value, {counts['pointer']} pointer output parameter(s), "
          f"{counts['unreasoned']} out-param comment(s) without a reason")
    return 0


if __name__ == "__main__":
    sys.exit(main())
