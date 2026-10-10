#!/bin/sh
# Screenshot tour through the showcase world (stage 21, decision E-220): one screenshot from every
# lookout and one close by for every area, from fixed camera positions that the world's plan gives
# (qtcraft-headless --showcase-tour), plus the light demo at night. The same content gives the same
# pictures, so the tour serves as visual check after changes; every new feature extends the world.
# Runs in the background on the built-in display (no focus, no captured mouse), silent, with its
# settings and world in a temporary directory that is removed at the end.
# Usage: scripts/showcase-tour.sh [out-dir] [build-dir]
#   TOUR_FILTER=<regex>  only stops whose name matches (e.g. 'demo-|creatures')
#   TOUR_FRAMES=<n>      frames after loading before the picture (liquids flow, sand falls; default 600)
set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$PROJECT_DIR/build/showcase-tour}
BUILD=${2:-$PROJECT_DIR/build/debug}
APP="$BUILD/src/app/qtcraft.app/Contents/MacOS/qtcraft"
HEADLESS="$BUILD/src/headless/qtcraft-headless"
FILTER=${TOUR_FILTER:-.}
FRAMES=${TOUR_FRAMES:-600}

if [ ! -x "$APP" ] || [ ! -x "$HEADLESS" ]; then
    echo "build $BUILD first (qtcraft and qtcraft-headless)" >&2
    exit 2
fi

DATA=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-tour.XXXXXX") || exit 2
trap 'rm -rf "$DATA"' EXIT
trap 'exit 130' INT TERM
mkdir -p "$OUT"
printf '[General]\nshowDebug=false\n[audio]\nmuted=true\n' >"$DATA/settings.ini"

"$HEADLESS" --showcase-tour --no-mods >"$DATA/stops.txt" || exit 1

# One picture: name, column, view and extra arguments.
shoot() {
    name=$1
    shift 5
    rm -f "$OUT/$name.png"
    env QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA" QTCRAFT_SCREENSHOT="$OUT/$name.png" QTCRAFT_SCREENSHOT_FRAMES="$FRAMES" \
        perl -e 'alarm shift; exec @ARGV or die' 180 "$APP" --showcase --no-audio --no-mods --screen builtin --background "$@" \
        >"$DATA/$name.log" 2>&1
    if [ -s "$OUT/$name.png" ]; then
        echo "$name: $OUT/$name.png"
    else
        echo "$name: no screenshot (log: $(tail -3 "$DATA/$name.log"))" >&2
        failed=1
    fi
}

failed=0
while read -r name x z yaw pitch; do
    echo "$name" | grep -Eq "$FILTER" || continue
    shoot "$name" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch"
    case "$name" in
    demo-light_row-near)
        shoot "$name-night" "$x" "$z" "$yaw" "$pitch" --spawn "$x,$z" --look "$yaw,$pitch" --chat "/time set night"
        ;;
    esac
done <"$DATA/stops.txt"

exit "$failed"
