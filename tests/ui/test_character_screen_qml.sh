#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# QML load test of the character screen (decision E-240): the real game opens the screen
# offscreen with a cape in the look and takes a screenshot; any QML warning or error (for example
# a broken binding of the cape list) fails the test. A second run saves a change with the pad and
# checks that the focus stays in the screen.
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

# Saving with the pad's A on "Save" disables the button; the focus moves on to "Random" in the
# editor (decision E-852), so the pad's B (Esc) still closes the screen.
QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA" QTCRAFT_SCREENSHOT="$DATA/save.png" QTCRAFT_SCREENSHOT_FRAMES=30 \
    perl -e 'alarm shift; exec @ARGV or die' 100 "$APP" --showcase --no-audio --no-mods --screen builtin --background \
    --overlay character --overlay "@600 click:characterRandom" --overlay "@900 pad:dpadUp" --overlay "@1200 focus:characterSave" \
    --overlay "@1500 pad:south" --overlay "@1800 pad:east" >"$DATA/save.txt" 2>&1
status=$?
if [ "$status" -ne 0 ]; then
    echo "game failed (exit $status) in the save run:"
    tail -20 "$DATA/save.txt"
    exit 1
fi
if [ "$(grep 'padFocus' "$DATA/save.txt" | tail -2 | head -1)" != "padFocus: characterRandom" ]; then
    echo "after saving, the focus left the editor:"
    grep 'padFocus' "$DATA/save.txt"
    exit 1
fi
if grep 'padFocus' "$DATA/save.txt" | tail -1 | grep -q 'padFocus: character'; then
    echo "after saving, the pad's B did not close the screen:"
    grep 'padFocus' "$DATA/save.txt"
    exit 1
fi
echo "saving keeps the focus in the editor"
