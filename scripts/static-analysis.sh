#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs the size limits checks (file and function length), the warning-only checks of output parameters,
# member access, repeated code, the QML heuristics (E-942) and the code style rules of E-379 (E-948), the
# API documentation check of the documented folders (comments, qdoc), clang-tidy, clazy and qmllint over
# the project (or over the given files).
# Usage: scripts/static-analysis.sh [--cheap-all] [build-dir] [file ...]
# Given files may be .cpp, .h, .mm and .qml, absolute or relative (to the project when the file is
# there, else to the current directory; the same for the build directory, which may be left out). clang-tidy and clazy analyse a header through its .cpp, else
# through the first built .cpp that includes it (directly or through up to two headers); a header no
# built .cpp includes and a .mm file skip those two tools with a note (every other check still runs,
# the last line names the skipped files). QML files are linted one by one. Exit 0 clean, 1 findings or a tool that failed,
# 2 a usage or setup error (missing build tree, file or analysis tool, a .cpp that is not built).
# --cheap-all (the full verify, decision E-929): the size limits and the warning-only checks run over the
# whole tree, clang-tidy, clazy and qmllint only over the given files (none given: none of them run).
# Every tool run is time-limited; the script exits non-zero when any tool reports findings.

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
CHEAP_ALL=0
if [ "${1:-}" = --cheap-all ]; then
    CHEAP_ALL=1
    shift
fi
# The first argument is the build directory unless it names a source file (or nothing): then the
# default build directory is used and every argument is a file.
BUILD_DIR="$PROJECT_DIR/build/debug"
if [ $# -gt 0 ]; then
    case "$1" in
    (*.cpp | *.h | *.qml | *.mm)
        if [ -d "$1" ]; then
            BUILD_DIR=$1
            shift
        fi
        ;;
    (*)
        BUILD_DIR=$1
        shift
        ;;
    esac
fi
# A relative build directory counts from the project (the checkout of this script) when it exists
# there, else from the current directory.
case "$BUILD_DIR" in
(/*) ;;
(*)
    if [ -d "$PROJECT_DIR/$BUILD_DIR" ] || [ ! -d "$BUILD_DIR" ]; then
        BUILD_DIR="$PROJECT_DIR/$BUILD_DIR"
    else
        BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
    fi
    ;;
esac

CREATOR_CLANG="$HOME/Qt/Qt Creator.app/Contents/Resources/libexec/clang/bin"
CLANG_TIDY=${CLANG_TIDY:-"$CREATOR_CLANG/clang-tidy"}
CLAZY=${CLAZY:-"$CREATOR_CLANG/clazy-standalone"}
# The Qt the build tree was configured with (its qmllint reads that Qt's qmltypes), else the newest
# installed Qt 6.
QT_PREFIX=$(sed -n 's#^Qt6_DIR:PATH=\(.*\)/lib/cmake/Qt6$#\1#p' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null)
[ -n "$QT_PREFIX" ] || QT_PREFIX=$(ls -d "$HOME"/Qt/6.*/macos 2>/dev/null | sort -V | tail -n 1)
QMLLINT=${QMLLINT:-"$QT_PREFIX/bin/qmllint"}
SDK_PATH=$(xcrun --show-sdk-path)
JOBS=${JOBS:-16}
TIME_LIMIT=${TIME_LIMIT:-900}
CLAZY_CHECKS=${CLAZY_CHECKS:-level0,level1,level2,no-qstring-allocations,no-ctor-missing-parent-argument,no-copyable-polymorphic,no-non-pod-global-static,no-rule-of-three}

# Runs a command in its own process group and kills the whole group when the time limit expires.
run_limited() {
    perl -e 'my $limit = shift; my $pid = fork(); if ($pid == 0) { setpgrp(0, 0); exec @ARGV or die; }
             local $SIG{ALRM} = sub { kill "TERM", -$pid; waitpid($pid, 0); exit 142; };
             alarm $limit; waitpid($pid, 0); exit($? >> 8);' "$TIME_LIMIT" "$@"
}

if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "compile_commands.json missing in $BUILD_DIR" >&2
    exit 2
fi

# A given file as an absolute path: relative to the project when it is there (the checkout of this
# script, also when it is called from another checkout), else to the current directory.
absolute_path() {
    case "$1" in
    (/*) echo "$1" ;;
    (*)
        if [ -e "$PROJECT_DIR/$1" ]; then
            echo "$PROJECT_DIR/$1"
        elif [ -e "$1" ]; then
            echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
        else
            echo "$PROJECT_DIR/$1"
        fi
        ;;
    esac
}

# Whether a source is one for another platform (decision E-834): *_other.cpp is the counterpart of a
# *_mac.mm, *_win.cpp and *_posix.cpp are built only there.
platform_source() {
    case "$1" in
    (*_other.cpp | *_win.cpp | *_posix.cpp) return 0 ;;
    esac
    return 1
}

# Whether the build tree compiles the given source (it has a compile command).
is_built() {
    grep -qF "\"file\": \"$1\"" "$BUILD_DIR/compile_commands.json"
}

# The include spelling of a project file: its path below src/ or tests/ (E-418).
include_name() {
    name=${1#"$PROJECT_DIR"/}
    name=${name#src/}
    echo "${name#tests/}"
}

# The files of the given suffix (cpp or h) that include one of the given files directly.
includers() {
    suffix=$1
    shift
    patterns=$(for file in "$@"; do echo "#include \"$(include_name "$file")\""; done)
    printf '%s\n' "$patterns" | grep -rlF --include="*.$suffix" -f /dev/stdin "$PROJECT_DIR/src" \
        "$PROJECT_DIR/tests" 2>/dev/null | LC_ALL=C sort
}

# The translation unit clang-tidy and clazy analyse a header through: the built .cpp of the same name,
# else the first built .cpp that includes it, directly or through at most two other headers; nothing
# when there is none.
header_unit() {
    if is_built "${1%.h}.cpp"; then
        echo "${1%.h}.cpp"
        return
    fi
    headers=$1
    for _depth in 1 2 3; do
        # shellcheck disable=SC2086
        for unit in $(includers cpp $headers); do
            if is_built "$unit"; then
                echo "$unit"
                return
            fi
        done
        # shellcheck disable=SC2086
        headers=$(includers h $headers)
        [ -n "$headers" ] || return
    done
}

# The given files become absolute paths (the tools and the compile commands know only those); a file
# that does not exist is a usage error.
for file do
    shift
    absolute=$(absolute_path "$file")
    if [ ! -f "$absolute" ]; then
        echo "no such file: $file" >&2
        exit 2
    fi
    case "$absolute" in
    ("$PROJECT_DIR"/*) ;;
    (*) echo "warning: $file is outside this checkout ($PROJECT_DIR); the build tree may not know it" >&2 ;;
    esac
    set -- "$@" "$absolute"
done

SELECTED=0
# Files of the selection clang-tidy and clazy cannot analyse, with the reason; named in their section
# and in the last line.
TOOL_SKIPPED=""
if [ $# -gt 0 ] || [ "$CHEAP_ALL" -eq 1 ]; then
    SELECTED=1
    LENGTH_FILES=$(printf '%s\n' "$@" | grep -E '\.(cpp|h|qml|mm)$')
    QML_FILES=$(printf '%s\n' "$@" | grep -E '\.qml$')
    UNITS=$(for file in "$@"; do
        case "$file" in
        (*.cpp) echo "unit $file" ;;
        (*.h)
            if [ -f "${file%.h}.cpp" ] && platform_source "${file%.h}.cpp" && ! is_built "${file%.h}.cpp"; then
                echo "skip $file (its source is not built on this platform)"
                continue
            fi
            unit=$(header_unit "$file")
            if [ -n "$unit" ]; then
                echo "unit $unit"
            else
                echo "skip $file (header that no built .cpp includes, also not through two other headers)"
            fi
            ;;
        (*.mm) echo "skip $file (Objective-C++, not analysed by clang-tidy and clazy)" ;;
        esac
    done)
    FILES=$(printf '%s\n' "$UNITS" | sed -n 's/^unit //p' | sort -u)
    TOOL_SKIPPED=$(printf '%s\n' "$UNITS" | sed -n 's/^skip //p')
else
    LENGTH_FILES=$(cd "$PROJECT_DIR" && { git ls-files 'src/*' 'tests/*'; git ls-files --others --exclude-standard 'src/*' 'tests/*'; } | grep -E '\.(cpp|h|qml|mm)$' | sort -u | sed "s|^|$PROJECT_DIR/|")
    QML_FILES=""
    FILES=$(cd "$PROJECT_DIR" && { git ls-files 'src/*.cpp' 'tests/*.cpp'; git ls-files --others --exclude-standard 'src/*.cpp' 'tests/*.cpp'; } | sort -u | sed "s|^|$PROJECT_DIR/|")
fi
# The cheap checks see a selection only without --cheap-all.
CHEAP_SELECTED=$SELECTED
if [ "$CHEAP_ALL" -eq 1 ]; then
    CHEAP_SELECTED=0
fi

status=0

echo "== file length"
# The limits live in scripts/check-file-length.sh; a selection without C++ or QML files checks none.
if [ "$CHEAP_SELECTED" -eq 0 ]; then
    "$PROJECT_DIR/scripts/check-file-length.sh" || status=1
elif [ -n "$LENGTH_FILES" ]; then
    # shellcheck disable=SC2086
    "$PROJECT_DIR/scripts/check-file-length.sh" $LENGTH_FILES || status=1
fi

echo "== function length"
# Code lines per function body (E-760), scripts/check-function-length.py; QML files are not checked.
FUNCTION_FILES=$(printf '%s\n' $LENGTH_FILES | grep -E '\.(cpp|h|mm)$')
if [ "$CHEAP_SELECTED" -eq 0 ]; then
    python3 "$PROJECT_DIR/scripts/check-function-length.py" || status=1
elif [ -n "$FUNCTION_FILES" ]; then
    # shellcheck disable=SC2086
    python3 "$PROJECT_DIR/scripts/check-function-length.py" $FUNCTION_FILES || status=1
fi

# Output parameters (rule in CLAUDE.md): counts only, never a finding that fails the run.
if [ "$CHEAP_SELECTED" -eq 0 ]; then
    echo "== output parameters (warning only)"
    python3 "$PROJECT_DIR/scripts/check-out-params.py" | tail -n 1
fi

# Members used in more than two places without getter/setter (rule in CLAUDE.md, E-891): warning only.
# The whole tree prints the counts, a selection lists the findings of the classes in the given files.
echo "== member access (warning only)"
if [ "$CHEAP_SELECTED" -eq 0 ]; then
    python3 "$PROJECT_DIR/scripts/check-member-access.py" | tail -n 1
elif [ -n "$FUNCTION_FILES" ]; then
    # shellcheck disable=SC2086
    python3 "$PROJECT_DIR/scripts/check-member-access.py" $FUNCTION_FILES
fi

# Repeated code (clones and blocks repeated back to back in C++ and QML) and the QML heuristics (long
# functions, Connections writing the root's state, enum lists, ids named like properties, unread object
# names, raw sizes), decision E-942: a NOLINT marker without a reason, a finding of a gating rule
# (GATING in check-qml.py) and a count above scripts/check-baseline.json (ratchet) fail. The whole tree
# prints the counts, a selection lists the findings that touch the given files.
echo "== duplicates (ratchet)"
if [ "$CHEAP_SELECTED" -eq 0 ]; then
    if ! CHECK_OUTPUT=$(python3 "$PROJECT_DIR/scripts/check-duplicates.py" --list); then
        status=1
        printf '%s\n' "$CHECK_OUTPUT" | grep -E '^[^ ]+:[0-9]+: .*without a reason|^ratchet:'
    fi
    printf '%s\n' "$CHECK_OUTPUT" | tail -n 1
elif [ -n "$LENGTH_FILES" ]; then
    # shellcheck disable=SC2086
    python3 "$PROJECT_DIR/scripts/check-duplicates.py" $LENGTH_FILES || status=1
fi
echo "== QML heuristics (gating rules and ratchet)"
if [ "$CHEAP_SELECTED" -eq 0 ]; then
    if ! CHECK_OUTPUT=$(python3 "$PROJECT_DIR/scripts/check-qml.py" --list); then
        status=1
        printf '%s\n' "$CHECK_OUTPUT" | grep -E '^[^ ]+:[0-9]+: .*without a reason|\[raw-size\]|^ratchet:'
    fi
    printf '%s\n' "$CHECK_OUTPUT" | tail -n 1
elif [ -n "$QML_FILES" ]; then
    # shellcheck disable=SC2086
    python3 "$PROJECT_DIR/scripts/check-qml.py" $QML_FILES || status=1
fi

# Code style rules of E-379 (std::string, hand-written delete, lock types, mutexes without a named
# hand-over), decision E-948: clang-query over the translation units (cached per unit in the build
# directory). Every finding fails since package 50b (limit 0 per rule), so does a NOLINT marker without
# a reason or a unit clang-query cannot process. The step has its own time limit (an uncached whole tree takes minutes), and a
# timeout only warns. Jobs follow the slot broker's grant (QC_TEST_JOBS). Without Homebrew LLVM it is
# skipped. The whole tree prints the counts, a selection lists the findings in the given files.
echo "== AST checks"
CLANG_QUERY=${CLANG_QUERY:-/opt/homebrew/opt/llvm/bin/clang-query}
export CLANG_QUERY
AST_SOURCES=$(printf '%s\n' $LENGTH_FILES | grep -E '\.(cpp|h|mm)$')
AST_JOBS=${QC_TEST_JOBS:-$JOBS}
if [ ! -x "$CLANG_QUERY" ]; then
    echo "AST checks skipped (no clang-query at $CLANG_QUERY)"
elif [ "$CHEAP_SELECTED" -eq 0 ] || [ -n "$AST_SOURCES" ]; then
    SAVED_TIME_LIMIT=$TIME_LIMIT
    TIME_LIMIT=${AST_TIME_LIMIT:-1800}
    if [ "$CHEAP_SELECTED" -eq 0 ]; then
        CHECK_OUTPUT=$(run_limited python3 "$PROJECT_DIR/scripts/check-ast.py" --list \
            --jobs "$AST_JOBS" --build-dir "$BUILD_DIR")
        ast_status=$?
        printf '%s\n' "$CHECK_OUTPUT" | grep -E '^[^ ]+:[0-9]+: \['
        printf '%s\n' "$CHECK_OUTPUT" | tail -n 1
    else
        # shellcheck disable=SC2086
        run_limited python3 "$PROJECT_DIR/scripts/check-ast.py" --jobs "$AST_JOBS" \
            --build-dir "$BUILD_DIR" $AST_SOURCES
        ast_status=$?
    fi
    TIME_LIMIT=$SAVED_TIME_LIMIT
    if [ "$ast_status" -eq 142 ]; then
        echo "AST checks timed out after ${AST_TIME_LIMIT:-1800} s (the timeout only warns)"
    elif [ "$ast_status" -ne 0 ]; then
        status=1
    fi
fi

# QDoc comments of the fully documented folders (scripts/doc-folders.txt, decision E-939). The comment
# check (about 1 s) always covers those whole folders; qdoc (about 40-70 s) runs on the whole tree and in
# the full verify (--cheap-all), and for a selection only when it holds a header of one of those folders
# (the comments live in the headers). Both fail on any finding or qdoc warning in those folders.
echo "== API documentation (documented folders)"
python3 "$PROJECT_DIR/scripts/convert-doc-comments.py" --check --documented || status=1
DOC_FOLDERS=$(sed -e 's/#.*//' -e 's/[[:space:]]//g' -e '/^$/d' -e 's|/*$|/|' "$PROJECT_DIR/scripts/doc-folders.txt")
DOC_HEADERS=$(for file in $LENGTH_FILES; do
    relative=${file#"$PROJECT_DIR"/}
    for folder in $DOC_FOLDERS; do
        case "$relative" in
        ("$folder"*.h) echo "$relative" ;;
        esac
    done
done)
if [ "$CHEAP_SELECTED" -eq 0 ] || [ -n "$DOC_HEADERS" ]; then
    DOC_LOG=$(mktemp)
    if run_limited python3 "$PROJECT_DIR/scripts/api-docs.py" --compile-commands "$BUILD_DIR/compile_commands.json" \
        --documented --max-warnings 0 >"$DOC_LOG" 2>&1; then
        tail -n 1 "$DOC_LOG"
    else
        cat "$DOC_LOG"
        status=1
    fi
    rm -f "$DOC_LOG"
else
    echo "qdoc skipped: no header of a documented folder in the selection"
fi

for tool in "$CLANG_TIDY" "$CLAZY"; do
    if [ ! -x "$tool" ]; then
        echo "analysis tool not found: $tool" >&2
        exit 2
    fi
done

FILE_COUNT=$(printf '%s\n' $FILES | grep -c '\.cpp$')
if [ "$FILE_COUNT" -eq 0 ] && [ "$SELECTED" -eq 0 ]; then
    echo "no source files to analyse" >&2
    exit 2
fi

# Sources for other platforms (*_other.cpp, the counterpart of a *_mac.mm; *_win.cpp and *_posix.cpp,
# decision E-834) are not built here.
ANALYSED=""
for file in $FILES; do
    if ! grep -qF "\"file\": \"$file\"" "$BUILD_DIR/compile_commands.json"; then
        if platform_source "$file"; then
            echo "skipped, not built on this platform: $file"
            continue
        fi
        echo "not in compile_commands.json (not built?): $file" >&2
        exit 2
    fi
    ANALYSED="$ANALYSED $file"
done
FILES=$ANALYSED
FILE_COUNT=$(printf '%s\n' $FILES | grep -c '\.cpp$')

LOG_DIR=$(mktemp -d)

# Fails when a tool run aborted (timeout, crash, missing compile command) instead of producing findings.
check_tool_run() {
    tool_name=$1
    exit_code=$2
    log=$3
    if grep -qE 'Error while processing|Skipping .*compile command|error: unable to|No such file' "$log"; then
        echo "$tool_name could not process all files:" >&2
        grep -E 'Error while processing|Skipping|error: unable to|No such file' "$log" | sort -u >&2
        status=1
    fi
    if [ "$exit_code" -ge 124 ] || [ "$exit_code" -eq 142 ]; then
        echo "$tool_name aborted (exit $exit_code)" >&2
        status=1
    fi
}

if [ -n "$TOOL_SKIPPED" ]; then
    echo "== clang-tidy and clazy skip $(printf '%s\n' "$TOOL_SKIPPED" | grep -c .) given files"
    printf '%s\n' "$TOOL_SKIPPED" | sed 's/^/skipped: /'
fi
if [ "$FILE_COUNT" -gt 0 ]; then
    echo "== clang-tidy ($FILE_COUNT files)"
    printf '%s\n' $FILES | run_limited xargs -P "$JOBS" -n 1 "$CLANG_TIDY" -p "$BUILD_DIR" --quiet \
        --header-filter="$PROJECT_DIR/(src|tests)/.*" \
        --extra-arg=-isysroot"$SDK_PATH" > "$LOG_DIR/tidy.log" 2>&1
    check_tool_run clang-tidy $? "$LOG_DIR/tidy.log"
    grep -E 'warning:|error:' "$LOG_DIR/tidy.log" | grep -v 'Error while processing' | sort -u > "$LOG_DIR/tidy.findings"
    if [ -s "$LOG_DIR/tidy.findings" ]; then
        cat "$LOG_DIR/tidy.findings"
        status=1
    fi

    echo "== clazy ($FILE_COUNT files)"
    printf '%s\n' $FILES | run_limited xargs -P "$JOBS" -n 1 "$CLAZY" -p "$BUILD_DIR" -checks="$CLAZY_CHECKS" \
        --extra-arg=-isysroot"$SDK_PATH" --extra-arg=-Wno-unknown-warning-option \
        --header-filter="$PROJECT_DIR/(src|tests)/.*" > "$LOG_DIR/clazy.log" 2>&1
    check_tool_run clazy $? "$LOG_DIR/clazy.log"
    grep -E 'warning:|error:' "$LOG_DIR/clazy.log" | grep -v '/build/' | sort -u > "$LOG_DIR/clazy.findings"
    if [ -s "$LOG_DIR/clazy.findings" ]; then
        cat "$LOG_DIR/clazy.findings"
        status=1
    fi
fi

if [ "$SELECTED" -eq 0 ]; then
    echo "== qmllint"
    # qmllint exits with 0 on warnings, so its log counts as well.
    if ! run_limited cmake --build "$BUILD_DIR" --target all_qmllint > "$LOG_DIR/qmllint.log" 2>&1 \
        || grep -qE '^(Warning|Error|Critical):' "$LOG_DIR/qmllint.log"; then
        grep -vE '^\[|^ninja' "$LOG_DIR/qmllint.log"
        status=1
    fi
elif [ -n "$QML_FILES" ]; then
    # The options of the build's qmllint target (import paths, resources) without its file list.
    RSP="$BUILD_DIR/src/app/.rcc/qmllint/qc_app_core.rsp"
    if [ ! -f "$RSP" ]; then
        echo "qmllint options missing: $RSP" >&2
        status=1
    else
        echo "== qmllint ($(printf '%s\n' $QML_FILES | grep -c .) files)"
        grep -v '\.qml$' "$RSP" > "$LOG_DIR/qmllint.rsp"
        # One call per file: qmllint shares one importer across the files of a call and loses the base
        # types of a file that is linted and imported by another one (bogus warnings such as "Could not
        # find property" on ItemSlot). The subshell fails when any call fails.
        # shellcheck disable=SC2086
        if ! (cd "$PROJECT_DIR/src/app" || exit 1
              failed=0
              for file in $QML_FILES; do
                  run_limited "$QMLLINT" "@$LOG_DIR/qmllint.rsp" "$file" || failed=1
              done
              exit $failed) > "$LOG_DIR/qmllint.log" 2>&1 \
            || grep -qE '^(Warning|Error|Critical):' "$LOG_DIR/qmllint.log"; then
            cat "$LOG_DIR/qmllint.log"
            status=1
        fi
    fi
fi

rm -rf "$LOG_DIR"
if [ $status -eq 0 ]; then
    if [ -n "$TOOL_SKIPPED" ]; then
        echo "static analysis clean (clang-tidy and clazy skipped $(printf '%s\n' "$TOOL_SKIPPED" | grep -c .) given files, see above)"
    else
        echo "static analysis clean"
    fi
fi
exit $status
