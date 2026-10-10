#!/bin/sh
# QML load test of the character screen (decision E-240): the real game opens the screen
# offscreen with a cape in the look and takes a screenshot; any QML warning or error (for example
# a broken binding of the cape list) fails the test.
# Usage: test_character_screen_qml.sh <qtcraft executable>
set -u
APP=$1
DATA=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-qmltest.XXXXXX") || exit 2
trap 'rm -rf "$DATA"' EXIT
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n[character]\ncape=qtcraft:green_cape\n' >"$DATA/settings.ini"

QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA" QTCRAFT_SCREENSHOT="$DATA/shot.png" QTCRAFT_SCREENSHOT_FRAMES=30 \
    perl -e 'alarm shift; exec @ARGV or die' 100 "$APP" --showcase --no-audio --no-mods --screen builtin --background \
    --overlay character --preview animation=walk,time=0.4 >"$DATA/log.txt" 2>&1
status=$?

if [ "$status" -ne 0 ] || [ ! -s "$DATA/shot.png" ]; then
    echo "game failed (exit $status) or took no screenshot:"
    tail -20 "$DATA/log.txt"
    exit 1
fi
if grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: ' "$DATA/log.txt"; then
    echo "QML reported problems"
    exit 1
fi
if ! grep -q 'Character preview' "$DATA/log.txt"; then
    echo "the character screen drew no preview:"
    tail -20 "$DATA/log.txt"
    exit 1
fi
echo "character screen loaded"
