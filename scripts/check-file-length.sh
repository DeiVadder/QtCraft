#!/bin/sh
# Checks the file length limits (lines) of C++ and QML sources: sources and QML 800, headers 300,
# tests 1000. A file may opt out only with a line "NOLINT(qc-file-length): <reason>".
# Without arguments it checks every tracked or new .cpp/.h/.qml/.mm file below src/ and tests/.
# Run by scripts/static-analysis.sh and the pre-push hook (tools/githooks/pre-push).
# Usage: scripts/check-file-length.sh [file ...]   (exit code 0: within the limits, 1: findings)

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SOURCE_LINE_LIMIT=800
HEADER_LINE_LIMIT=300
TEST_LINE_LIMIT=1000

if [ $# -gt 0 ]; then
    FILES=$(printf '%s\n' "$@" | grep -E '\.(cpp|h|qml|mm)$')
else
    FILES=$(cd "$PROJECT_DIR" && { git ls-files 'src/*' 'tests/*'; git ls-files --others --exclude-standard 'src/*' 'tests/*'; } | grep -E '\.(cpp|h|qml|mm)$' | sort -u | sed "s|^|$PROJECT_DIR/|")
fi

# Prints "<file> <lines> <limit>" for every file of the "<lines> <file>" lines of wc above its limit.
over_limit() {
    while read -r lines file; do
        [ "$file" = total ] && continue
        case "$file" in
        "$PROJECT_DIR"/tests/* | tests/*) limit=$TEST_LINE_LIMIT ;;
        *.h) limit=$HEADER_LINE_LIMIT ;;
        *) limit=$SOURCE_LINE_LIMIT ;;
        esac
        [ "$lines" -gt "$limit" ] && echo "$file $lines $limit"
    done
}

# One wc over all files; only files above their limit are searched for the opt-out line.
status=0
OVER=$(for file in $FILES; do [ -f "$file" ] && echo "$file"; done | xargs wc -l 2>/dev/null | over_limit)

for entry in $(printf '%s\n' "$OVER" | tr ' ' '|'); do
    file=${entry%%|*}
    rest=${entry#*|}
    if ! grep -q 'NOLINT(qc-file-length): ' "$file"; then
        echo "$file: ${rest%%|*} lines (limit ${rest#*|})"
        status=1
    fi
done
exit $status
