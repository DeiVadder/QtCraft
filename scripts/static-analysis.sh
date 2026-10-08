#!/bin/sh
# Runs clang-tidy, clazy and qmllint over the project (or over the given source files).
# Usage: scripts/static-analysis.sh [build-dir] [file ...]
# Every tool run is time-limited; the script exits non-zero when any tool reports findings.

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR=${1:-"$PROJECT_DIR/build/debug"}
[ $# -gt 0 ] && shift

CREATOR_CLANG="$HOME/Qt/Qt Creator.app/Contents/Resources/libexec/clang/bin"
CLANG_TIDY=${CLANG_TIDY:-"$CREATOR_CLANG/clang-tidy"}
CLAZY=${CLAZY:-"$CREATOR_CLANG/clazy-standalone"}
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

if [ $# -gt 0 ]; then
    FILES=$(printf '%s\n' "$@" | grep -E '\.cpp$')
else
    FILES=$(cd "$PROJECT_DIR" && { git ls-files 'src/*.cpp' 'tests/*.cpp'; git ls-files --others --exclude-standard 'src/*.cpp' 'tests/*.cpp'; } | sort -u | sed "s|^|$PROJECT_DIR/|")
fi

for tool in "$CLANG_TIDY" "$CLAZY"; do
    if [ ! -x "$tool" ]; then
        echo "analysis tool not found: $tool" >&2
        exit 2
    fi
done

FILE_COUNT=$(printf '%s\n' $FILES | grep -c '\.cpp$')
if [ "$FILE_COUNT" -eq 0 ]; then
    echo "no source files to analyse" >&2
    exit 2
fi

# Sources for other platforms (*_other.cpp, the counterpart of a *_mac.mm) are not built here.
ANALYSED=""
for file in $FILES; do
    if ! grep -qF "\"file\": \"$file\"" "$BUILD_DIR/compile_commands.json"; then
        case "$file" in
        *_other.cpp)
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
status=0

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

echo "== qmllint"
if ! run_limited cmake --build "$BUILD_DIR" --target all_qmllint > "$LOG_DIR/qmllint.log" 2>&1; then
    grep -vE '^\[|^ninja' "$LOG_DIR/qmllint.log"
    status=1
fi

rm -rf "$LOG_DIR"
[ $status -eq 0 ] && echo "static analysis clean"
exit $status
