#!/bin/sh
# Layout test of the HUD while playing: the real game renders a flat world offscreen with armour
# worn (hearts, food, level and armour rows over the hotbar) and several chat lines, one of them
# far wider than the window, at the smallest window (1024 x 640) and at 1400 x 900 (larger windows are limited to the screen). The layout
# check (QTCRAFT_LAYOUT_CHECK) must find no item outside the window and no status row under the
# chat lines; a missing check line or a QML warning fails the test too.
# Usage: test_hud_layout.sh <qtcraft executable> [directory for the screenshots]
set -u
APP=$1
DATA=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-hudtest.XXXXXX") || exit 2
trap 'rm -rf "$DATA"' EXIT
SHOTS=${2:-$DATA}
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$DATA/settings.ini"
long="A chat line far longer than the window is wide, so that it has to wrap instead of running across the hearts, the food and the armour: lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua."
failed=0

# check <width>x<height>: one offscreen run of the HUD at that window size.
check() {
    size=$1
    QTCRAFT_WINDOW_SIZE=$size QTCRAFT_LAYOUT_CHECK=1 \
        QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA" QTCRAFT_SCREENSHOT="$SHOTS/hud-$size.png" QTCRAFT_SCREENSHOT_FRAMES=60 \
        perl -e 'alarm shift; exec @ARGV or die' 90 "$APP" --no-audio --no-mods --screen builtin --background \
        --play --world-type flat --view-distance 2 \
        --chat "@200 /equip diamond" --chat "@300 /xp add Player 5 levels" \
        --chat "@400 Hello" --chat "@500 $long" --chat "@600 Third line" >"$DATA/$size.log" 2>&1
    status=$?

    if [ "$status" -ne 0 ] || [ ! -s "$SHOTS/hud-$size.png" ]; then
        echo "$size: game failed (exit $status) or took no screenshot:"
        tail -20 "$DATA/$size.log"
        failed=1
    elif ! grep -q "Layout check: 0 items outside the window $size" "$DATA/$size.log"; then
        echo "$size: items outside the window:"
        grep -E 'Outside the window|Layout check' "$DATA/$size.log"
        failed=1
    elif ! grep -q 'HUD check: chat lines overlap 0 status rows' "$DATA/$size.log"; then
        echo "$size: chat lines overlap the status rows:"
        grep -E 'Chat lines overlap|HUD check' "$DATA/$size.log"
        failed=1
    elif grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: ' "$DATA/$size.log"; then
        echo "$size: QML reported problems"
        failed=1
    else
        echo "$size: chat above the status rows, everything inside the window"
    fi
}

check 1024x640
check 1400x900
exit $failed
