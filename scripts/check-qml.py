#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Heuristic checks of the QML files below src/ (decision E-942): rules that were text only before.

Rules (name, what it finds):
  long-function        a JavaScript function or a handler block (`onX: { ... }`) of more than
                       --max-function-lines code lines (default 15): logic of that size belongs in C++
  connections-write    a handler of a `Connections` that writes a property declared on the file's root
                       object (`root.lastOverlay = ...`, with or without the root's id): state kept in the
                       window and driven by signals belongs in a controller
  enum-list            one expression that compares the same value with three or more enum values
                       (`a.overlay === T.Overlay.A || a.overlay === T.Overlay.B || ...`) or lists three or more
                       enum values in an array: a property or a function of the C++ type says it once
  id-shadows-property  an id with the name of a property declared or bound in the same file (`id: game` next
                       to `game: game`): which one a name means depends on the scope rules
  unread-object-name   `objectName: "x"` where `x` occurs nowhere else in src/, tests/, scripts/ or tools/
                       (a name built with `+` counts as read when its fixed part occurs elsewhere); not
                       checked on controls the pad focuses and the test hooks click (INTERACTIVE: buttons,
                       fields, switches, sliders, slots, lists), whose names are read at run time
  raw-size             a number above 1 in a size property (width, margins, spacing, radius, font size, ...)
                       outside `px()`/`hudPx()`/`scaled()` and not a factor (`2 * Ui.space.l`): sizes go through
                       `Ui.px`/`Ui.hudPx` (E-725). Not checked: `controls/Ui.qml` (it holds the values) and the
                       properties of a `Window`/`ApplicationWindow` (window sizes are not scaled)
  on-property          a declared property named `on` + capital letter (`property bool onParchment`): an
                       assignment `onParchment: true` reads like a signal handler (row 79.53, E-947)
  loader-item          `<loader id>.item` dereferenced (`.x`, `[..]`, `(id.item as T).x`; `?.` is fine) without a
                       check before it in the same block (the conditions of enclosing if/else/while/for bodies
                       included) or line: `id.status`, `id.item &&`/`||`/`?`/`=== null`, `!id.item`,
                       `if (... id.item)`, or for a synchronous Loader an `id.active = true` (the item is then
                       made at once). Synchronous: no `asynchronous` binding other than `false` and no
                       `id.asynchronous = ...` in the file. Only Loaders with an id in the same file; the
                       unqualified `item.x` inside a Loader's own bindings and handlers is not checked
  anchors-layout       an object with both `anchors` and `Layout.*` bindings: inside a layout the anchors fight
                       the layout, outside one the `Layout.*` values do nothing
  pad-command-string   a pad command handled as a string: `gamepadCommand`/`takeCommand` with a `string`
                       parameter, or a string literal compared or matched (`=== "back"`, `case "back":`) in
                       their bodies; the command is the enum GamepadMenuInput.Command (E-688, row 79.60)
  mode-string          a mode, kind or state of the menus handled as a string: one of the fields gameMode,
                       worldType (also as the id of a dropdown with its currentValue), generator, origin,
                       kind, keyLayout, keyboardLayout or modelData.state compared with a string literal (`world.gameMode === "creative"`), or a `switch` on one
                       with string `case`s; each is an enum (GameMode.Mode, WorldLibrary.WorldType,
                       TexturePack.Origin, HandbookModel.RecipeKind, GameSettings.KeyboardLayout,
                       ModStatus.State; E-688, row 79.62). Not seen: a function parameter of another
                       name (`switch (mode)`), other string fields (`language === "de"`) and string
                       `value:` entries in model arrays (row 79.65)
  screen-key           in a `Keys.onPressed` handler, Esc compared (`event.key === Qt.Key_Escape`) or the key
                       table asked (`keys.matches(...)`): what Esc, a screen's own key and the handbook key do
                       on a screen is one rule in C++, `KeyControls.screenKey` (row 79.58)
  input-check          input checked or made in JavaScript: `.trim()`, `parseInt(`, `parseFloat(` or
                       `Math.random(`; C++ takes the raw text, trims, parses and refuses it with a warning
                       (E-688), and draws random values (QcNewWorldDraft; row 79.64, E-956)
  image-source-size    an `Image`, `AnimatedImage` or `BorderImage` without `sourceSize`, so the whole picture is
                       decoded; not checked with `smooth: false` (pixel art: small sources drawn as they are, a
                       sourceSize only resamples)
A finding is skipped when its line or the line above holds `// NOLINT(qc-qml-<rule>): <reason>` (a marker
without a reason is listed itself as `unreasoned`). Heuristic on comment-blanked text, not a QML parser.
Gating rules (exit code 1 on a finding): those in GATING and markers without a reason. The rules in
scripts/check-baseline.json are ratcheted (scripts/qc_ratchet.py): their count over the whole tree may only
go down, a rise fails the run. Like check-duplicates.py the ratchet counts the whole tree also under
--changed and for a file selection (about 1 s more), so a selection fails on a rise elsewhere too; it is
not checked with another --max-function-lines. The others only print.
Usage: scripts/check-qml.py [--list] [--changed] [--max-function-lines N] [file ...]
  --list     prints every finding, otherwise only the counts per rule
  --changed  only QML files changed against dev (committed, staged, unstaged, new)
  file ...   only these files (implies --list; names elsewhere still count as reads)
  --self-test checks samples held in memory
"""

import importlib.util
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_BRANCH = os.environ.get("QC_BASE_BRANCH", "dev")
RULES = ("long-function", "connections-write", "enum-list", "id-shadows-property", "unread-object-name", "raw-size",
         "on-property", "loader-item", "anchors-layout", "image-source-size", "pad-command-string",
         "mode-string", "screen-key", "input-check")
# Rules whose findings fail the run (exit code 1): those at 0 that the human made blocking (row 79.48b,
# E-942; `enum-list` and `connections-write` since their findings in Main.qml went with 79.47); a NOLINT
# marker without a reason always fails.
GATING = {"raw-size", "id-shadows-property", "enum-list", "connections-write", "pad-command-string", "mode-string"}
# Properties every Item has (QQuickItem and QObject): an id with one of these names hides the member in
# every unqualified use inside the file (qmllint id-shadows-member), as `id: layer` hides `Item.layer`.
ITEM_MEMBERS = {
    "parent", "children", "data", "resources", "x", "y", "z", "width", "height", "implicitWidth",
    "implicitHeight", "opacity", "enabled", "visible", "visibleChildren", "states", "transitions", "state",
    "childrenRect", "anchors", "left", "right", "horizontalCenter", "top", "bottom", "verticalCenter",
    "baseline", "baselineOffset", "clip", "focus", "activeFocus", "activeFocusOnTab", "focusPolicy",
    "rotation", "scale", "transformOrigin", "transform", "smooth", "antialiasing", "layer",
    "containmentMask", "objectName",
}
# Code lines of a JS function or handler block before `long-function` reports it (E-942).
FUNCTION_LINES = 15
NOLINT = re.compile(r"NOLINT\(qc-qml-([\w-]+)\):(.*)")
READ_FOLDERS = ("src/", "tests/", "scripts/", "tools/")

OBJECT = re.compile(r"(?<![\w.])([A-Z][\w]*(?:\.[A-Z]\w*)*)\s*(?:on\s+[\w.]+\s*)?\{")
FUNCTION = re.compile(r"\bfunction\s+(\w+)\s*\([^)]*\)\s*(?::\s*[\w.<>]+\s*)?\{")
HANDLER_BLOCK = re.compile(r"^[ \t]*((?:[A-Z]\w*\.)*on[A-Z]\w*)\s*:\s*(?:function\s*\w*\s*\([^)]*\)\s*"
                           r"|\([^)]*\)\s*=>\s*|\w+\s*=>\s*)?\{", re.MULTILINE)
ID = re.compile(r"^[ \t]*id\s*:\s*(\w+)", re.MULTILINE)
DECLARATION = re.compile(r"^[ \t]*(?:(?:readonly|required|default)\s+)*property\s+(?:list\s*<[^>]*>|[\w.]+)\s+(\w+)",
                         re.MULTILINE)
BINDING = re.compile(r"^[ \t]*([a-z]\w*)\s*:(?!:)", re.MULTILINE)
ENUM_VALUE = r"[A-Z]\w*\.[A-Z]\w*\.[A-Z]\w*"
COMPARISON = re.compile(rf"([\w.?]+)\s*(?:===|!==|==|!=)\s*({ENUM_VALUE})\b|\b({ENUM_VALUE})\s*(?:===|!==|==|!=)\s*([\w.?]+)")
ENUM_ARRAY = re.compile(rf"\[\s*{ENUM_VALUE}(?:\s*,\s*{ENUM_VALUE}){{2,}}\s*,?\s*\]")
OBJECT_NAME = re.compile(r"\bobjectName\s*:\s*(\"[^\"\n]*\"|'[^'\n]*')(\s*\+)?")
SIZE_PROPERTIES = {
    "width", "height", "implicitWidth", "implicitHeight", "minimumWidth", "minimumHeight", "maximumWidth",
    "maximumHeight", "preferredWidth", "preferredHeight", "margins", "leftMargin", "rightMargin", "topMargin",
    "bottomMargin", "horizontalCenterOffset", "verticalCenterOffset", "spacing", "rowSpacing", "columnSpacing",
    "padding", "leftPadding", "rightPadding", "topPadding", "bottomPadding", "horizontalPadding",
    "verticalPadding", "inset", "leftInset", "rightInset", "topInset", "bottomInset", "radius", "pixelSize",
    "pointSize", "x", "y", "contentWidth", "contentHeight", "cellWidth", "cellHeight", "strokeWidth",
}
SIZE_NAME = re.compile(r"(?:[Ww]idth|[Hh]eight|Margin|[Ss]pacing|[Pp]adding|[Rr]adius|[Gg]ap|Inset|Offset)$")
SIZE_LINE = re.compile(r"^[ \t]*(?:(?:readonly|required|default)\s+)*(property\s+[\w.]+\s+)?([\w.]+)\s*:(?!:)(.*)$",
                       re.MULTILINE)
NUMBER = re.compile(r"(?<![\w.])\d+(?:\.\d+)?(?![\w.])")
SCALED_CALL = re.compile(r"\b(?:px|hudPx|scaled)\s*\(")
WINDOW_TYPES = {"Window", "ApplicationWindow"}
ON_NAME = re.compile(r"on[A-Z]")
# Bindings of the new rules: at the start of a line or after `{`/`;` (`Loader { id: lazy; active: false }`).
BINDING_START = r"(?:^|(?<=[{;]))[ \t]*"
LOADER_ID = re.compile(BINDING_START + r"id\s*:\s*(\w+)", re.MULTILINE)
ASYNCHRONOUS = re.compile(BINDING_START + r"asynchronous\s*:\s*(?!false\b)", re.MULTILINE)
IMAGE_TYPES = {"Image", "AnimatedImage", "BorderImage"}
ANCHORS = re.compile(BINDING_START + r"anchors\s*[.{]", re.MULTILINE)
LAYOUT = re.compile(BINDING_START + r"Layout\s*[.{]", re.MULTILINE)
SOURCE_SIZE = re.compile(BINDING_START + r"sourceSize\s*[.:{]", re.MULTILINE)
# `smooth: false` draws pixel art at its own small size (icons of 16 px, the pixel-art pictures, maps): a
# sourceSize would only resample it, so such an Image is no finding.
PIXEL_ART = re.compile(BINDING_START + r"smooth\s*:\s*false\b", re.MULTILINE)
# Controls the pad can focus and the test hooks click: their names are read at run time (the focus
# description of QcGamepadUi, `click:<objectName>`), so an unread name there is no finding.
# The functions that take a pad command (QcMenuNavigator calls gamepadCommand, NavList.takeCommand
# gets it passed on), and a string literal compared or matched in their bodies.
PAD_FUNCTION = re.compile(r"\bfunction\s+(gamepadCommand|takeCommand)\s*\(([^)]*)\)\s*(?::\s*[\w.<>]+\s*)?\{")
STRING_TEST = re.compile(r"(?:===|!==|==|!=)\s*[\"']|[\"']\s*(?:===|!==|==|!=)|\bcase\s+[\"']")
# The fields of the menus' modes, kinds and states (rule mode-string).
MODE_FIELD = (r"\b(?:(?:gameMode|worldType)(?:\.currentValue)?|generator|origin|kind|keyLayout|keyboardLayout"
              r"|modelData\.state)\b")
MODE_STRING = re.compile(MODE_FIELD + r"\s*(?:===|!==|==|!=)\s*[\"']|[\"']\s*(?:===|!==|==|!=)\s*[\w.?]*" + MODE_FIELD)
MODE_SWITCH = re.compile(r"\bswitch\s*\([\w.?]*" + MODE_FIELD + r"\s*\)\s*\{")
# A `Keys.onPressed` handler, and in its body Esc compared for equality or the key table asked.
KEYS_PRESSED = re.compile(r"\bKeys\.onPressed\s*:")
INPUT_CHECK = re.compile(r"\.trim\s*\(\s*\)|\bparseInt\s*\(|\bparseFloat\s*\(|\bMath\.random\s*\(")
SCREEN_KEY_TEST = re.compile(r"(?<![!=])(?:===|==)\s*Qt\.Key_Escape\b|\bQt\.Key_Escape\s*(?:===|==)|\bkeys\.matches\s*\(")
INTERACTIVE = re.compile(r"(?:Button|Field|Switch|Slider|Dropdown|Choice|Check|CheckBox|ComboBox|Segments|NavList"
                         r"|Slot|TextEdit|TextInput|Flickable|ListView|GridView)$")


def load_module(name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), f"{name}.py")
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


QC_CODE_LINES = load_module("qc_code_lines")
QC_RATCHET = load_module("qc_ratchet")


def matching(text, start):
    """Index of the brace closing the one at start (len(text) - 1 when unbalanced)."""
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


def line_of(text, offset):
    return text.count("\n", 0, offset) + 1


class QmlFile:
    """One QML file: the raw text, the text with comments blanked and string contents replaced by `x`,
    and the objects (type, `{`, `}`) found in it."""

    def __init__(self, path, text):
        self.path = path
        self.text = text
        self.masked = QC_CODE_LINES.masked(text, path)
        self.objects = []
        for match in OBJECT.finditer(self.masked):
            brace = match.end() - 1
            self.objects.append((match.group(1), brace, matching(self.masked, brace)))
        self.object_braces = {entry[1] for entry in self.objects}
        self.pairs = self.braces()

    def braces(self):
        """Every brace pair of the masked text as {open: close}."""
        pairs, stack = {}, []
        for index, character in enumerate(self.masked):
            if character == "{":
                stack.append(index)
            elif character == "}" and stack:
                pairs[stack.pop()] = index
        return pairs

    def in_object(self, offset):
        """True when the innermost brace around offset opens a QML object (not a JavaScript block)."""
        inner = [start for start, end in self.pairs.items() if start < offset <= end]
        return bool(inner) and max(inner) in self.object_braces

    def enclosing(self, offset):
        """The innermost object around offset as (type, open, close), or None."""
        inside = [entry for entry in self.objects if entry[1] < offset <= entry[2]]
        return min(inside, key=lambda entry: entry[2] - entry[1]) if inside else None

    def root(self):
        return self.objects[0] if self.objects else None

    def depth_one(self, pattern):
        """Matches of pattern directly inside the root object (not in a nested object)."""
        root = self.root()
        if root is None:
            return []
        return [match for match in pattern.finditer(self.masked, root[1], root[2])
                if self.enclosing(match.start()) == root]


def long_functions(qml, limit):
    found = []
    for pattern in (FUNCTION, HANDLER_BLOCK):
        for match in pattern.finditer(qml.masked):
            brace = qml.masked.index("{", match.end() - 1)
            end = matching(qml.masked, brace)
            body = qml.masked[brace + 1:end]
            lines = sum(1 for line in body.split("\n") if line.strip())
            if lines > limit:
                found.append((line_of(qml.masked, match.start()), "long-function",
                              f"{match.group(1)} has {lines} code lines (limit {limit}); move the logic to C++"))
    return found


def connections_writes(qml):
    root = qml.root()
    if root is None:
        return []
    ids = [match.group(1) for match in qml.depth_one(ID)]
    names = {match.group(1) for match in qml.depth_one(DECLARATION)}
    if not names:
        return []
    declared = "|".join(re.escape(name) for name in sorted(names))
    targets = [rf"\b{re.escape(name)}\s*\.\s*({declared})\b" for name in ids] + [rf"(?<![\w.])({declared})\b"]
    write = re.compile(rf"(?:{'|'.join(targets)})\s*(?:=(?!=)|\+=|-=|\+\+|--)")
    found = []
    for kind, start, end in qml.objects:
        if kind != "Connections" or (kind, start, end) == root:
            continue
        for match in write.finditer(qml.masked, start, end):
            name = next(group for group in match.groups() if group)
            if re.search(r"\b(?:var|let|const)\s*$", qml.masked[max(start, match.start() - 8):match.start()]):
                continue
            found.append((line_of(qml.masked, match.start()), "connections-write",
                          f"Connections writes {name} of the root object; keep that state in a controller"))
    return found


def expressions(masked):
    """(offset, text) of the expressions of a masked QML text: split at `;`, braces and line breaks that
    do not continue an expression (a line ending in an operator or the next starting with one)."""
    ends_open = re.compile(r"(?:&&|\|\||[(\[,?:+\-*/=!<>])[ \t]*$")
    starts_open = re.compile(r"\n[ \t]*(?:&&|\|\||\?|:(?!\w)|\.|\+|-(?!-)|\))")
    pieces, start, line_start = [], 0, 0
    for index, character in enumerate(masked):
        if character == "\n":
            open_line = ends_open.search(masked, line_start, index) or starts_open.match(masked, index)
            line_start = index + 1
            if open_line:
                continue
        elif character not in ";{}":
            continue
        pieces.append((start, masked[start:index]))
        start = index + 1
    pieces.append((start, masked[start:]))
    return pieces


def enum_lists(qml):
    found = []
    for offset, text in expressions(qml.masked):
        values = {}
        for match in COMPARISON.finditer(text):
            subject, value = (match.group(1), match.group(2)) if match.group(1) else (match.group(4), match.group(3))
            values.setdefault(subject, set()).add(value)
        listed = [subject for subject, seen in values.items() if len(seen) >= 3]
        arrays = ENUM_ARRAY.findall(text)
        for subject in listed:
            found.append((line_of(qml.masked, offset + len(text) - len(text.lstrip())), "enum-list",
                          f"{subject} compared with {len(values[subject])} enum values; one property or function in C++"))
        if arrays and not listed:
            found.append((line_of(qml.masked, offset + len(text) - len(text.lstrip())), "enum-list",
                          "array of enum values; one property or function in C++"))
    return found


def shadowing_ids(qml):
    names = {match.group(1) for match in DECLARATION.finditer(qml.masked)} | ITEM_MEMBERS
    names |= {match.group(1) for match in BINDING.finditer(qml.masked)
              if qml.in_object(match.start())} - {"id", "default", "case"}
    found = []
    for match in ID.finditer(qml.masked):
        if match.group(1) in names:
            found.append((line_of(qml.masked, match.start()), "id-shadows-property",
                          f"id {match.group(1)} has the name of a property in this file or of Item; rename the id"))
    return found


def unread_object_names(qml, corpus):
    found = []
    for match in OBJECT_NAME.finditer(qml.masked):
        literal = qml.text[match.start(1) + 1:match.end(1) - 1]
        prefix = bool(match.group(2))
        if not literal or not re.match(r"^[\w:.-]+$", literal):
            continue
        owner = qml.enclosing(match.start())
        if owner is not None and INTERACTIVE.search(owner[0]):
            continue
        line = line_of(qml.masked, match.start())
        uses = corpus.count(literal, (qml.path, line), prefix)
        if uses == 0:
            found.append((line, "unread-object-name",
                          f"objectName \"{literal}\" is read nowhere (src/, tests/, scripts/, tools/); remove it"))
    return found


def outside_calls(expression):
    """The expression with the arguments of px()/hudPx()/scaled() calls blanked."""
    out = list(expression)
    for match in SCALED_CALL.finditer(expression):
        depth, index = 0, match.end() - 1
        while index < len(expression):
            if expression[index] == "(":
                depth += 1
            elif expression[index] == ")":
                depth -= 1
                if depth == 0:
                    break
            if depth > 0 and index >= match.end():
                out[index] = " "
            index += 1
    return "".join(out)


def raw_sizes(qml):
    if qml.path.endswith("controls/Ui.qml"):
        return []
    found = []
    for match in SIZE_LINE.finditer(qml.masked):
        name = match.group(2).split(".")[-1]
        declared = bool(match.group(1))
        if not (name in SIZE_PROPERTIES and not declared) and not (declared and SIZE_NAME.search(name)):
            continue
        owner = qml.enclosing(match.start())
        if owner is not None and owner[0] in WINDOW_TYPES and not declared:
            continue
        expression = outside_calls(match.group(3))
        for number in NUMBER.finditer(expression):
            before = expression[:number.start()].rstrip()
            after = expression[number.end():].lstrip()
            operand = before.endswith(("*", "/", "<", ">", "=", "[", "%")) or after.startswith(("*", "/", "<", ">", "=", "!", "]", "%"))
            if float(number.group()) <= 1 or operand:
                continue
            found.append((line_of(qml.masked, match.start()), "raw-size",
                          f"{match.group(2)}: {number.group()} without Ui.px/Ui.hudPx (E-725)"))
            break
    return found


def direct_bindings(qml, owner, pattern):
    """Matches of pattern at the start of a line directly inside the object owner (type, open, close):
    not in a nested object and not in a JavaScript block."""
    return [match for match in pattern.finditer(qml.masked, owner[1], owner[2])
            if qml.enclosing(match.start()) == owner and qml.in_object(match.start())]


def handler_properties(qml):
    found = []
    for match in DECLARATION.finditer(qml.masked):
        if ON_NAME.match(match.group(1)):
            found.append((line_of(qml.masked, match.start()), "on-property",
                          f"property {match.group(1)} reads like a signal handler; rename it"))
    return found


def loader_ids(qml):
    """{id: synchronous} of the Loader objects of the file."""
    loaders = {}
    for owner in qml.objects:
        if owner[0] != "Loader":
            continue
        ids = direct_bindings(qml, owner, LOADER_ID)
        if ids:
            name = ids[0].group(1)
            assigned = re.search(rf"(?<![\w.]){re.escape(name)}\.asynchronous\s*=(?!=)", qml.masked)
            loaders[name] = not direct_bindings(qml, owner, ASYNCHRONOUS) and not assigned
    return loaders


def control_body(qml, brace):
    """True when the brace opens the body of an if, else, while or for (a block whose condition guards it)."""
    before = qml.masked[:brace].rstrip()
    if re.search(r"\belse$", before):
        return True
    if not before.endswith(")"):
        return False
    depth = 0
    for index in range(len(before) - 1, -1, -1):
        depth += {")": 1, "(": -1}.get(before[index], 0)
        if depth == 0:
            return bool(re.search(r"\b(?:if|while|for)\s*$", before[:index]))
    return False


def guard_context(qml, offset):
    """The text before offset that can hold a check: the enclosing JavaScript block up to offset, where the
    bodies of if/else/while/for count as part of the block around them (so their conditions are in it), or
    the line up to offset for a binding directly in an object."""
    inner = sorted((start for start, end in qml.pairs.items() if start < offset <= end), reverse=True)
    start = next((brace for brace in inner if brace in qml.object_braces or not control_body(qml, brace)), 0)
    if start in qml.object_braces:
        start = qml.masked.rfind("\n", 0, offset) + 1
    return qml.masked[start:offset]


def unchecked_loader_items(qml):
    found = []
    for name, synchronous in loader_ids(qml).items():
        escaped = re.escape(name)
        guard = (rf"\b{escaped}\.status\b|\b{escaped}\.item\s*(?:&&|\|\||\?(?!\.)|[!=]==?\s*null)"
                 rf"|!\s*{escaped}\.item\b|\b(?:if|while)\s*\([^;{{}}]*\b{escaped}\.item\s*\)")
        if synchronous:
            guard += rf"|\b{escaped}\.active\s*=\s*true\b"
        use = re.compile(rf"(?<![\w.]){escaped}\.item\b(?:\s+as\s+[\w.]+\s*\))?\s*(?:\.|\[)")
        for match in use.finditer(qml.masked):
            if re.search(guard, guard_context(qml, match.start())):
                continue
            found.append((line_of(qml.masked, match.start()), "loader-item",
                          f"{name}.item used without checking {name}.status or {name}.item; it is null "
                          "until the Loader is ready"))
    return found


def pad_command_strings(qml):
    found = []
    for match in PAD_FUNCTION.finditer(qml.masked):
        line = line_of(qml.masked, match.start())
        if re.search(r":\s*string\b", match.group(2)):
            found.append((line, "pad-command-string",
                          f"{match.group(1)} takes a string; take the command as int (GamepadMenuInput.Command)"))
        brace = match.end() - 1
        for test in STRING_TEST.finditer(qml.masked, brace, matching(qml.masked, brace)):
            found.append((line_of(qml.masked, test.start()), "pad-command-string",
                          f"{match.group(1)} compares with a string; compare with GamepadMenuInput.Command"))
    return found


def mode_strings(qml):
    found = [(line_of(qml.masked, match.start()), "mode-string",
              "a mode, kind or state compared with a string; compare with its enum (E-688)")
             for match in MODE_STRING.finditer(qml.masked)]
    for match in MODE_SWITCH.finditer(qml.masked):
        brace = match.end() - 1
        for test in STRING_TEST.finditer(qml.masked, brace, matching(qml.masked, brace)):
            found.append((line_of(qml.masked, test.start()), "mode-string",
                          "a switch on a mode, kind or state with string cases; use its enum (E-688)"))
    return found


def screen_keys(qml):
    found = []
    for match in KEYS_PRESSED.finditer(qml.masked):
        line_end = qml.masked.find("\n", match.end())
        line_end = len(qml.masked) if line_end < 0 else line_end
        brace = qml.masked.find("{", match.end(), line_end)
        end = matching(qml.masked, brace) if brace >= 0 else line_end
        for test in SCREEN_KEY_TEST.finditer(qml.masked, match.end(), end):
            found.append((line_of(qml.masked, test.start()), "screen-key",
                          "a screen's key rule in QML; ask KeyControls.screenKey (Esc, own key, handbook key)"))
    return found


def input_checks(qml):
    return [(line_of(qml.masked, match.start()), "input-check",
             "input trimmed, parsed or drawn in JavaScript; let C++ check it and refuse it with a warning (E-688)")
            for match in INPUT_CHECK.finditer(qml.masked)]


def anchors_with_layout(qml):
    found = []
    for owner in qml.objects:
        anchors = direct_bindings(qml, owner, ANCHORS)
        if anchors and direct_bindings(qml, owner, LAYOUT):
            found.append((line_of(qml.masked, anchors[0].start()), "anchors-layout",
                          f"{owner[0]} has anchors and Layout.* properties; a layout and anchors fight over "
                          "the geometry"))
    return found


def images_without_source_size(qml):
    found = []
    for owner in qml.objects:
        if owner[0] not in IMAGE_TYPES or direct_bindings(qml, owner, SOURCE_SIZE) or direct_bindings(qml, owner, PIXEL_ART):
            continue
        found.append((line_of(qml.masked, owner[1]), "image-source-size",
                      f"{owner[0]} without sourceSize decodes the whole picture; set sourceSize"))
    return found


class Corpus:
    """The texts in which an object name counts as read: every file below READ_FOLDERS, indexed by word
    (a run of letters, digits, `_`, `:`, `.` and `-`) and by the parts of a word between `:` and `.`, so
    `open:menuSettingsScreen` (a test hook) and `dialog.deleteWorldDialog` read their names."""

    WORD = re.compile(r"[\w:.-]*\w")
    PARTS = re.compile(r"[:.]")

    def __init__(self, texts):
        self.texts = texts
        self.words = {}
        for text in texts.values():
            for word in self.terms(text):
                self.words[word] = self.words.get(word, 0) + 1
        self.joined = "\n".join(texts.values())

    @classmethod
    def terms(cls, text):
        """The words of text and, for a word with `:` or `.`, each of its parts."""
        found = []
        for word in cls.WORD.findall(text):
            found.append(word)
            parts = [part for part in cls.PARTS.split(word) if part]
            if len(parts) > 1 or (parts and parts[0] != word):
                found.extend(parts)
        return found

    def count(self, name, defining, prefix):
        """Occurrences of name (as a word or a part of one, or anywhere when prefix) outside the line
        `defining` (path, line)."""
        line = self.texts.get(defining[0], "").split("\n")[defining[1] - 1] if defining[0] in self.texts else ""
        if prefix:
            return self.joined.count(name) - line.count(name)
        return self.words.get(name, 0) - self.terms(line).count(name)


def markers(text):
    """(line, rule) -> True when the marker gives a reason."""
    found = {}
    for number, line in enumerate(text.split("\n"), start=1):
        match = NOLINT.search(line)
        if match:
            found[(number, match.group(1))] = bool(match.group(2).strip())
    return found


def check_file(path, text, corpus, limit=FUNCTION_LINES):
    """The findings of one QML file: (path, line, rule, message), markers applied."""
    qml = QmlFile(path, text)
    findings = (long_functions(qml, limit) + connections_writes(qml) + enum_lists(qml) + shadowing_ids(qml)
                + unread_object_names(qml, corpus) + raw_sizes(qml) + handler_properties(qml)
                + unchecked_loader_items(qml) + anchors_with_layout(qml) + images_without_source_size(qml)
                + pad_command_strings(qml) + mode_strings(qml) + screen_keys(qml) + input_checks(qml))
    marks = markers(text)
    reasoned = {key for key, reason in marks.items() if reason}
    kept = [(path, line, rule, message) for line, rule, message in sorted(set(findings))
            if (line, rule) not in reasoned and (line - 1, rule) not in reasoned]
    kept += [(path, line, "unreasoned", f"NOLINT(qc-qml-{rule}): without a reason")
             for (line, rule), reason in sorted(marks.items()) if not reason]
    return kept


def git_files(*arguments):
    output = subprocess.run(["git", *arguments], cwd=ROOT, capture_output=True, text=True, check=False).stdout
    return [line for line in output.split("\n") if line]


def read_corpus():
    texts = {}
    for path in git_files("ls-files", "--cached", "--others", "--exclude-standard", *READ_FOLDERS):
        full = os.path.join(ROOT, path)
        if os.path.isfile(full) and os.path.getsize(full) < 2_000_000:
            try:
                with open(full, encoding="utf-8") as file:
                    texts[path] = file.read()
            except UnicodeDecodeError:
                continue
    return texts


def changed_files():
    """QML files below src/ changed against the base branch: committed, staged, unstaged and new."""
    files = set(git_files("diff", "--name-only", f"{BASE_BRANCH}...HEAD"))
    files |= set(git_files("diff", "--name-only", "HEAD"))
    files |= set(git_files("ls-files", "--others", "--exclude-standard"))
    return {path for path in files if path.startswith("src/") and path.endswith(".qml")}


def main(arguments):
    if "--self-test" in arguments:
        return self_test()
    limit = FUNCTION_LINES
    if "--max-function-lines" in arguments:
        limit = int(arguments[arguments.index("--max-function-lines") + 1])
    files = {os.path.relpath(os.path.abspath(path), ROOT) for path in arguments
             if not path.startswith("--") and path != str(limit)}
    selected = {path for path in files if path.endswith(".qml")} or (
        changed_files() if "--changed" in arguments else None)
    texts = read_corpus()
    corpus = Corpus(texts)
    qml_files = sorted(path for path in texts if path.startswith("src/") and path.endswith(".qml"))
    findings = []
    for path in qml_files:
        if selected is None or path in selected:
            findings += check_file(path, texts[path], corpus, limit)
    risen = 0
    if limit == FUNCTION_LINES:
        tree = findings if selected is None else [finding for path in qml_files
                                                  for finding in check_file(path, texts[path], corpus, limit)]
        risen = QC_RATCHET.check("check-qml.py", {rule: sum(1 for finding in tree if finding[2] == rule)
                                                  for rule in RULES})
    if "--list" in arguments or selected is not None:
        for path, line, rule, message in findings:
            print(f"{path}:{line}: [{rule}] {message}")
    gated = 0
    for rule in RULES + ("unreasoned",):
        number = sum(1 for finding in findings if finding[2] == rule)
        gate = rule in GATING or rule == "unreasoned"
        gated += number if gate else 0
        print(f"{rule}: {number}" + ("" if gate else " (warning only)"))
    print(f"QML checks (E-942): {len(findings)} findings in {len(qml_files) if selected is None else len(selected)} "
          f"files, {gated} in gating rules")
    return 1 if gated or risen else 0


SAMPLE = """import QtQuick
ApplicationWindow {
    id: root
    width: 1280
    property int lastOverlay: 0
    property real gap: 12
    property real wide: Ui.px(400) + 2 * Ui.space.l

    GameController {
        id: game
        objectName: "game"
        visible: game.overlay === GameController.Overlay.A || game.overlay === GameController.Overlay.B
                 || game.overlay === GameController.Overlay.C
    }
    Hud {
        objectName: "unusedName"
        game: game
        anchors.margins: 8
        radius: Ui.px(4)
        opacity: 0.5
        // NOLINT(qc-qml-raw-size): sample exemption
        spacing: 6
    }
    Connections {
        target: game
        function onOverlayChanged() {
            root.lastOverlay = game.overlay;
            const local = 1;
        }
    }
    function long() {
        let a = 1;
        a += 1;
        a += 2;
        return a;
    }
}
"""

OTHER_FILES = {"src/app/Reader.cpp": 'findChild<QObject *>(u"game"_s);'}


def self_test():
    corpus = Corpus({"src/app/qml/Main.qml": SAMPLE, **OTHER_FILES})
    found = check_file("src/app/qml/Main.qml", SAMPLE, corpus, limit=3)
    got = sorted((line, rule) for _, line, rule, _ in found)
    expected = sorted([(6, "raw-size"), (10, "id-shadows-property"), (12, "enum-list"), (16, "unread-object-name"),
                       (18, "raw-size"), (27, "connections-write"), (31, "long-function")])
    failures = [] if got == expected else [f"expected {expected}, got {got}"]
    shadow = "import QtQuick\nItem {\n    id: layer\n}\n"
    got = [rule for _, _, rule, _ in check_file("src/app/qml/Layer.qml", shadow, corpus, 3)]
    if got != ["id-shadows-property"]:
        failures.append(f"an id named like a member of Item is listed: {got}")
    marked = SAMPLE.replace("// NOLINT(qc-qml-raw-size): sample exemption", "// NOLINT(qc-qml-raw-size):")
    got = sorted((line, rule) for _, line, rule, _ in check_file("src/app/qml/Main.qml", marked, corpus, 3))
    if (21, "unreasoned") not in got or (22, "raw-size") not in got:
        failures.append(f"marker without a reason is listed and exempts nothing: {got}")
    hooks = {"tests/ui/test_menu_layout.sh": 'run --overlay "@300 open:menuSettingsScreen"',
             "src/app/Reader.cpp": "dialog.deleteWorldDialog->open();"}
    names = """import QtQuick
Item {
    Item { objectName: "menuSettingsScreen" }
    Item { objectName: "deleteWorldDialog" }
    Item { objectName: "nobodyReadsThis" }
}
"""
    got = [(line, rule) for _, line, rule, _ in check_file("src/app/qml/Names.qml", names,
                                                           Corpus({"src/app/qml/Names.qml": names, **hooks}))]
    if got != [(5, "unread-object-name")]:
        failures.append(f"names read through open:<name> and a member path count as read: {got}")
    handlers = """import QtQuick
Item {
    Component.onCompleted: {
        let a = 1;
        a += 1;
        a += 2;
        return a;
    }
    onClicked: (mouse) => {
        let b = 1;
        b += 1;
        b += 2;
        return b;
    }
    onPressed: function(event) {
        let c = 1;
        c += 1;
        c += 2;
        return c;
    }
}
"""
    got = [(line, rule) for _, line, rule, _ in check_file("src/app/qml/Handlers.qml", handlers, Corpus({}), 3)]
    if got != [(3, "long-function"), (9, "long-function"), (15, "long-function")]:
        failures.append(f"dotted, arrow and function handler blocks are long functions: {got}")
    quiet = """import QtQuick
Item {
    id: root
    property int state: 0
    Text {
        id: columns
        font.pixelSize: root.state > 2 ? Ui.px(12) : Ui.px(10)
        function settings() { return { columns: 2 }; }
    }
    Connections {
        target: root
        function onWidthChanged() { columns.opacity = 0; }
    }
    Button {
        objectName: "clickedByName"
    }
}
"""
    got = check_file("src/app/qml/Quiet.qml", quiet, Corpus({"src/app/qml/Quiet.qml": quiet}), 3)
    if got:
        failures.append(f"comparison, JavaScript key, child property and control name are no findings: {got}")
    qt_rules = """import QtQuick
import QtQuick.Layouts
Item {
    property bool onParchment: false
    property bool online: false
    Loader { id: lazy; asynchronous: true }
    Loader { id: eager; active: false }
    width: lazy.item.width
    height: lazy.item ? lazy.item.height : 0
    readonly property Item shown: lazy.item as Item
    function show() {
        (lazy.item as Item).visible = true;
        (lazy.item as Item)?.forceActiveFocus();
        if (lazy.status === Loader.Ready)
            lazy.item.opacity = 1;
        eager.active = true;
        eager.item.visible = true;
    }
    function reveal() {
        lazy.active = true;
        lazy.item.visible = true;
    }
    function braced() {
        if (lazy.item) {
            lazy.item.visible = true;
        }
        if (lazy.status === Loader.Ready) {
            lazy.item.x = 1;
        }
    }
    function logged() {
        console.log(lazy.item);
        lazy.item.y = 1;
    }
    Loader { id: flagged; asynchronous: root.slow }
    Loader { id: switched; asynchronous: false }
    Loader { id: plain }
    function late() {
        switched.asynchronous = true;
        flagged.active = true;
        flagged.item.visible = true;
        switched.active = true;
        switched.item.visible = true;
        plain.active = true;
        plain.item.visible = true;
    }
    Rectangle {
        anchors.fill: parent
        Layout.fillWidth: true
    }
    Rectangle {
        anchors { left: parent.left }
        Layout.preferredWidth: parent.width
        Item { Layout.fillWidth: true }
    }
    Image { source: "photo.png" }
    Image { source: "photo.png"; sourceSize.width: 64 }
    Image { source: "icon.png"; smooth: false }
    // NOLINT(qc-qml-image-source-size): sample exemption
    Image { source: "small.png" }
    BorderImage { source: "frame.png" }
}
"""
    got = [(line, rule) for _, line, rule, _ in check_file("src/app/qml/QtRules.qml", qt_rules, Corpus({}), 30)]
    expected = [(4, "on-property"), (8, "loader-item"), (12, "loader-item"), (21, "loader-item"),
                (33, "loader-item"), (41, "loader-item"), (43, "loader-item"),
                (48, "anchors-layout"), (52, "anchors-layout"), (56, "image-source-size"),
                (61, "image-source-size")]
    if got != expected:
        failures.append(f"on-property, loader-item, anchors-layout and image-source-size: expected {expected}, got {got}")
    pad = """import QtQuick
Item {
    function gamepadCommand(command: string): bool {
        if (command === "back")
            return true;
        switch (command) {
        case 'tabNext':
            return true;
        }
        return command === GamepadMenuInput.Command.Up || name === "x";
    }
    function takeCommand(command: int): bool {
        return command === GamepadMenuInput.Command.TabNext;
    }
    function other(mode: string): bool {
        return mode === "creative";
    }
}
"""
    got = [(line, rule) for _, line, rule, _ in check_file("src/app/qml/Pad.qml", pad, Corpus({}), 30)]
    expected = [(3, "pad-command-string"), (4, "pad-command-string"), (7, "pad-command-string"),
                (10, "pad-command-string")]
    if got != expected:
        failures.append(f"pad-command-string: expected {expected}, got {got}")
    modes = """\
Item {
    readonly property bool flat: world.generator === "flat"
    readonly property bool loaded: "loaded" !== modRow.modelData.state
    readonly property bool creative: world.gameMode === GameMode.Mode.Creative
    readonly property bool terrain: worldType.currentValue !== "terrain"
    readonly property bool pressed: button.state === "pressed"
    function text(world: var): string {
        switch (world?.worldType) {
        case "void":
            return "";
        }
        return world.kind === HandbookModel.RecipeKind.Machine ? "" : "-";
    }
}
"""
    got = [(line, rule) for _, line, rule, _ in check_file("src/app/qml/Modes.qml", modes, Corpus({}), 30)]
    expected = [(2, "mode-string"), (3, "mode-string"), (5, "mode-string"), (9, "mode-string")]
    if got != expected:
        failures.append(f"mode-string: expected {expected}, got {got}")
    keys = """import QtQuick
Item {
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape)
            close();
        else if (game.settings.keys.matches("handbook", event.key))
            open();
        event.accepted = event.key !== Qt.Key_Tab;
    }
    Item {
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape
    }
    Item {
        Keys.onPressed: event => close(Qt.Key_Escape == event.key)
    }
    function other(key: int): bool {
        return key === Qt.Key_Escape;
    }
}
"""
    got = [(line, rule) for _, line, rule, _ in check_file("src/app/qml/Keys.qml", keys, Corpus({}), 30)]
    expected = [(4, "screen-key"), (6, "screen-key"), (14, "screen-key")]
    if got != expected:
        failures.append(f"screen-key: expected {expected}, got {got}")
    inputs = """Item {
    function a() { return field.text.trim(); }
    readonly property int port: parseInt(text) + parseFloat(other)
    // field.text.trim() in a comment
    readonly property real seed: Math.random() * Number(x)
}
"""
    got = [(line, rule) for _, line, rule, _ in check_file("src/app/qml/Inputs.qml", inputs, Corpus({}), 30)]
    expected = [(2, "input-check"), (3, "input-check"), (5, "input-check")]
    if got != expected:
        failures.append(f"input-check: expected {expected}, got {got}")
    for failure in failures:
        print(f"FAIL: {failure}")
    print("check-qml self-test: " + ("failed" if failures else "all samples judged correctly"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
