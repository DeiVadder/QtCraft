#!/bin/sh
# Layout test of the main menu in the smallest window (1024 x 640, Main.qml's minimum size): the
# real game renders the menu offscreen, alone and with each of its screens open, and the layout
# check (QTCRAFT_LAYOUT_CHECK) lists every shown item that reaches outside the window, such as a
# Quit button pushed below the bottom edge by a form taller than the window. Any such item, a
# missing check line or a QML warning fails the test.
# Usage: test_menu_layout.sh <qtcraft executable>
set -u
APP=$1
DATA=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-layouttest.XXXXXX") || exit 2
trap 'rm -rf "$DATA"' EXIT
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$DATA/settings.ini"
failed=0

# check <name> [qtcraft options...]: one offscreen run of the menu.
check() {
    name=$1
    shift
    QTCRAFT_WINDOW_SIZE=1024x640 QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_MENU_WORLD_TYPE=flat \
        QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA" QTCRAFT_SCREENSHOT="$DATA/$name.png" QTCRAFT_SCREENSHOT_FRAMES=20 \
        perl -e 'alarm shift; exec @ARGV or die' 60 "$APP" --no-audio --no-mods --screen builtin --background "$@" >"$DATA/$name.log" 2>&1
    status=$?

    if [ "$status" -ne 0 ] || [ ! -s "$DATA/$name.png" ]; then
        echo "$name: game failed (exit $status) or took no screenshot:"
        tail -20 "$DATA/$name.log"
        failed=1
    elif ! grep -q 'Layout check: 0 items outside the window 1024x640' "$DATA/$name.log"; then
        echo "$name: items outside the 1024 x 640 window:"
        grep -E 'Outside the window|Layout check' "$DATA/$name.log"
        failed=1
    elif grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: ' "$DATA/$name.log"; then
        echo "$name: QML reported problems"
        failed=1
    else
        echo "$name: everything inside the window"
    fi
}

check menu
check settings --overlay "@300 open:menuSettingsScreen"
check mods --overlay "@300 open:modsScreen"
check character --overlay "@300 open:menuCharacterScreen"
check multiplayer --overlay multiplayer
exit $failed
