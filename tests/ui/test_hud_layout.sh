#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Layout test of the HUD while playing: the real game renders a flat world offscreen with armour
# worn (hearts, food, level and armour rows over the hotbar) and several chat lines, one of them
# far wider than the window, at the smallest window (1024 x 640) and at 1400 x 900 (larger windows are limited to the screen). The layout
# check (QTCRAFT_LAYOUT_CHECK) must find no item outside the window and no status row under the
# chat lines; a missing check line or a QML warning fails the test too.
# Then the HUD behind the screens (decision E-927, "HUD parts shown" of the layout check) at
# 1280 x 720 with the UI scale at 100 % and 200 %: the chat lines, the world hint, the name of the
# block in view and the crosshair hide behind every screen, the hotbar and the hearts too behind
# the full-screen ones (settings, "Manage server", advancements, handbook); the chat line keeps all
# but the world hint, the death screen keeps the status rows, and closing a screen brings everything
# back.
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

all="chat 1 hint 1 target 1 crosshair 1 hotbar 1 hearts 1"
chatOpen="chat 1 hint 0 target 1 crosshair 1 hotbar 1 hearts 1"
statusRows="chat 0 hint 0 target 0 crosshair 0 hotbar 1 hearts 1"
nothing="chat 0 hint 0 target 0 crosshair 0 hotbar 0 hearts 0"

# parts <scale> <name> <expected parts> <arguments...>: one offscreen run at 1280 x 720 with chat
# lines and a block in view; the HUD parts shown must be the expected ones.
parts() {
    scale=$1
    name=$2
    expected=$3
    shift 3
    dir="$DATA/parts-$scale-$name"
    mkdir -p "$dir"
    printf '[General]\nuiScale=%s\n[debug]\nlevel=off\n[audio]\nmuted=true\n' "$scale" >"$dir/settings.ini"
    QTCRAFT_WINDOW_SIZE=1280x720 QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_SCREEN_KEEP_ALIVE_MS=0 \
        QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$dir" QTCRAFT_SCREENSHOT="$SHOTS/hud-$scale-$name.png" QTCRAFT_SCREENSHOT_FRAMES=30 \
        perl -e 'alarm shift; exec @ARGV or die' 90 "$APP" --no-audio --no-mods --screen builtin --background \
        --play --world-type flat --view-distance 2 --look 0,-30 \
        --chat "@200 Hello" --chat "@300 Second line" "$@" >"$DATA/parts-$scale-$name.log" 2>&1
    status=$?

    shown=$(grep -o 'HUD parts shown: .*' "$DATA/parts-$scale-$name.log" | tail -1 | sed 's/HUD parts shown: //')
    if [ "$status" -ne 0 ]; then
        echo "$scale % $name: game failed (exit $status):"
        tail -20 "$DATA/parts-$scale-$name.log"
        failed=1
    elif [ "$shown" != "$expected" ]; then
        echo "$scale % $name: HUD parts shown \"$shown\", expected \"$expected\""
        failed=1
    elif grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: ' "$DATA/parts-$scale-$name.log"; then
        echo "$scale % $name: QML reported problems"
        failed=1
    else
        echo "$scale % $name: $shown"
    fi
}

check 1024x640
check 1400x900
parts 100 world "$all"
parts 100 pause "$statusRows" --overlay "@1500 pause"
parts 100 settings "$nothing" --overlay "@1500 settings"
parts 100 handbook "$nothing" --overlay "@1500 handbook"
parts 100 handbook-closed "$all" --overlay "@1500 handbook" --overlay "@2500 "
parts 100 handbook-over-pause "$nothing" --overlay "@1500 pause" --overlay "@2000 handbook"
parts 100 chat "$chatOpen" --overlay "@1500 chat"
parts 100 death "$statusRows" --chat "@1500 /kill"
parts 200 chat "$chatOpen" --overlay "@1500 chat"
parts 200 inventory "$statusRows" --overlay "@1500 inventory"
parts 200 admin "$nothing" --overlay "@1500 admin"
parts 200 advancements "$nothing" --overlay "@1500 advancements"
parts 200 advancements-closed "$all" --overlay "@1500 advancements" --overlay "@2500 "
exit $failed
