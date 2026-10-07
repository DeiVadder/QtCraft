#!/bin/sh
# Full stage verification: debug build + tests, static analysis, ASan/UBSan and TSan builds + tests,
# and a time-limited smoke run of the real game that saves a screenshot.
# Usage: scripts/verify.sh [screenshot.png]

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2
SCREENSHOT=${1:-"${TMPDIR:-/tmp}/qtcraft-smoke.png"}
failures=""

step() {
    name=$1
    shift
    echo "== $name"
    if ! "$@"; then
        failures="$failures $name"
    fi
}

limited() {
    perl -e 'alarm shift; exec @ARGV or die' "$@"
}

for preset in debug asan tsan; do
    step "configure-$preset" limited 300 cmake --preset "$preset"
    step "build-$preset" limited 1200 cmake --build --preset "$preset"
    step "test-$preset" limited 1200 ctest --preset "$preset"
done

step "static-analysis" scripts/static-analysis.sh "$PROJECT_DIR/build/debug"

rm -f "$SCREENSHOT"
step "smoke-run" env QTCRAFT_SCREENSHOT="$SCREENSHOT" QTCRAFT_SCREENSHOT_FRAMES=60 \
    perl -e 'alarm shift; exec @ARGV or die' 120 build/debug/src/app/qtcraft.app/Contents/MacOS/qtcraft
step "smoke-screenshot" test -s "$SCREENSHOT"

# The renderer and UI code only runs in the app, so it also gets a sanitizer smoke run.
rm -f "$SCREENSHOT.asan.png"
step "smoke-run-asan" env QTCRAFT_SCREENSHOT="$SCREENSHOT.asan.png" QTCRAFT_SCREENSHOT_FRAMES=60 \
    perl -e 'alarm shift; exec @ARGV or die' 180 build/asan/src/app/qtcraft.app/Contents/MacOS/qtcraft
step "smoke-screenshot-asan" test -s "$SCREENSHOT.asan.png"

if [ -n "$failures" ]; then
    echo "FAILED:$failures"
    exit 1
fi
echo "ALL GREEN (screenshot: $SCREENSHOT)"
