#!/bin/sh
# Runs the size limits checks (file and function length), the warning-only checks of output parameters and
# member access, clang-tidy, clazy and qmllint over the project (or over the given files).
# Usage: scripts/static-analysis.sh [build-dir] [file ...]
# Given files may be .cpp, .h and .qml: a header is analysed through its .cpp, QML files are linted one by one.
# Every tool run is time-limited; the script exits non-zero when any tool reports findings.

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${1:-"$PROJECT_DIR/build/debug"}
[ $# -gt 0 ] && shift

CREATOR_CLANG="$HOME/Qt/Qt Creator.app/Contents/Resources/libexec/clang/bin"
CLANG_TIDY=${CLANG_TIDY:-"$CREATOR_CLANG/clang-tidy"}
CLAZY=${CLAZY:-"$CREATOR_CLANG/clazy-standalone"}
QMLLINT=${QMLLINT:-"$HOME/Qt/6.8.3/macos/bin/qmllint"}
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

SELECTED=0
if [ $# -gt 0 ]; then
    SELECTED=1
    LENGTH_FILES=$(printf '%s\n' "$@" | grep -E '\.(cpp|h|qml|mm)$')
    QML_FILES=$(printf '%s\n' "$@" | grep -E '\.qml$')
    # A header is analysed through the .cpp of the same name, when there is one.
    FILES=$(for file in "$@"; do
        if [ "${file%.cpp}" != "$file" ]; then
            echo "$file"
        elif [ "${file%.h}" != "$file" ] && [ -f "${file%.h}.cpp" ]; then
            echo "${file%.h}.cpp"
        fi
    done | sort -u)
else
    LENGTH_FILES=$(cd "$PROJECT_DIR" && { git ls-files 'src/*' 'tests/*'; git ls-files --others --exclude-standard 'src/*' 'tests/*'; } | grep -E '\.(cpp|h|qml|mm)$' | sort -u | sed "s|^|$PROJECT_DIR/|")
    QML_FILES=""
    FILES=$(cd "$PROJECT_DIR" && { git ls-files 'src/*.cpp' 'tests/*.cpp'; git ls-files --others --exclude-standard 'src/*.cpp' 'tests/*.cpp'; } | sort -u | sed "s|^|$PROJECT_DIR/|")
fi

status=0

echo "== file length"
# The limits live in scripts/check-file-length.sh; a selection without C++ or QML files checks none.
if [ "$SELECTED" -eq 0 ] || [ -n "$LENGTH_FILES" ]; then
    # shellcheck disable=SC2086
    "$PROJECT_DIR/scripts/check-file-length.sh" $LENGTH_FILES || status=1
fi

echo "== function length"
# Code lines per function body (E-760), scripts/check-function-length.py; QML files are not checked.
FUNCTION_FILES=$(printf '%s\n' $LENGTH_FILES | grep -E '\.(cpp|h|mm)$')
if [ "$SELECTED" -eq 0 ]; then
    python3 "$PROJECT_DIR/scripts/check-function-length.py" || status=1
elif [ -n "$FUNCTION_FILES" ]; then
    # shellcheck disable=SC2086
    python3 "$PROJECT_DIR/scripts/check-function-length.py" $FUNCTION_FILES || status=1
fi

# Output parameters (rule in CLAUDE.md): counts only, never a finding that fails the run.
if [ "$SELECTED" -eq 0 ]; then
    echo "== output parameters (warning only)"
    python3 "$PROJECT_DIR/scripts/check-out-params.py" | tail -n 1
fi

# Members used in more than two places without getter/setter (rule in CLAUDE.md, E-891): warning only.
# The whole tree prints the counts, a selection lists the findings of the classes in the given files.
echo "== member access (warning only)"
if [ "$SELECTED" -eq 0 ]; then
    python3 "$PROJECT_DIR/scripts/check-member-access.py" | tail -n 1
elif [ -n "$FUNCTION_FILES" ]; then
    # shellcheck disable=SC2086
    python3 "$PROJECT_DIR/scripts/check-member-access.py" $FUNCTION_FILES
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
        case "$file" in
        *_other.cpp | *_win.cpp | *_posix.cpp)
            echo "skipped, not built on this platform: $file"
            continue
            ;;
        esac
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
        # shellcheck disable=SC2086
        if ! (cd "$PROJECT_DIR/src/app" && run_limited "$QMLLINT" "@$LOG_DIR/qmllint.rsp" $QML_FILES) > "$LOG_DIR/qmllint.log" 2>&1 \
            || grep -qE '^(Warning|Error|Critical):' "$LOG_DIR/qmllint.log"; then
            cat "$LOG_DIR/qmllint.log"
            status=1
        fi
    fi
fi

rm -rf "$LOG_DIR"
[ $status -eq 0 ] && echo "static analysis clean"
exit $status
