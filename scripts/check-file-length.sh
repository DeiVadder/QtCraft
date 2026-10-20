#!/bin/sh
# Checks the file length limits of C++ and QML sources in code lines (E-760): sources, Objective-C++ and
# QML 600, headers 220 (also those of the tests), test sources 750. Blank lines and comments (doc
# comments included) do not count; the counting is scripts/qc_code_lines.py. A file may opt out only with a line "NOLINT(qc-file-length): <reason>".
# Without arguments it checks every tracked or new .cpp/.h/.qml/.mm file below src/ and tests/.
# Run by scripts/static-analysis.sh and the pre-push hook (tools/githooks/pre-push).
# Usage: scripts/check-file-length.sh [file ...]   (exit code 0: within the limits, 1: findings)

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SOURCE_LINE_LIMIT=600
HEADER_LINE_LIMIT=220
TEST_LINE_LIMIT=750

if [ $# -gt 0 ]; then
    FILES=$(printf '%s\n' "$@" | grep -E '\.(cpp|h|qml|mm)$')
else
    FILES=$(cd "$PROJECT_DIR" && { git ls-files 'src/*' 'tests/*'; git ls-files --others --exclude-standard 'src/*' 'tests/*'; } | grep -E '\.(cpp|h|qml|mm)$' | sort -u | sed "s|^|$PROJECT_DIR/|")
fi

# Prints "<file> <lines> <limit>" for every file of the "<code lines> <file>" lines above its limit.
over_limit() {
    while read -r lines file; do
        [ "$file" = total ] && continue
        # Headers first: a header of the tests has the header limit too.
        case "$file" in
        *.h) limit=$HEADER_LINE_LIMIT ;;
        "$PROJECT_DIR"/tests/* | tests/*) limit=$TEST_LINE_LIMIT ;;
        *) limit=$SOURCE_LINE_LIMIT ;;
        esac
        [ "$lines" -gt "$limit" ] && echo "$file $lines $limit"
    done
}

# One count over all files; only files above their limit are searched for the opt-out line.
status=0
OVER=$(for file in $FILES; do [ -f "$file" ] && echo "$file"; done | xargs python3 "$PROJECT_DIR/scripts/qc_code_lines.py" | over_limit)

for entry in $(printf '%s\n' "$OVER" | tr ' ' '|'); do
    file=${entry%%|*}
    rest=${entry#*|}
    if ! grep -q 'NOLINT(qc-file-length): ' "$file"; then
        echo "$file: ${rest%%|*} code lines (limit ${rest#*|})"
        status=1
    fi
done
exit $status
