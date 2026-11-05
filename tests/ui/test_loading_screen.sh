#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# The loading and start screens in the real game, offscreen (decision E-943). The test hook
# --overlay "loading:<stage>,..." shows the loading screen at a stage with counts, "@0 starting"
# the start screen; each run at 1024 x 640 and at 2048 x 1280 with the UI scale at 200 % must load
# without QML problems, keep everything inside the window and name every control, and show the
# title and the counts. A world started with --play must report when it became playable (the
# spawn area meshed) before its screenshot, without hitting the longest wait; a state shown by the
# hook in a playing world stays as it is (the world becomes playable once); a world started from
# the menu is pictured only once it plays.
# Usage: test_loading_screen.sh <qtcraft executable> <qtcraft-headless executable> <data directory>
set -u
APP=$1
HEADLESS=$2
DATA=$3
rm -rf "$DATA"
mkdir -p "$DATA"
failed=0

# run <name> <scale> <window size> <overlay> [more arguments]
run() {
    name=$1 scale=$2 size=$3 overlay=$4
    shift 4
    mkdir -p "$DATA/$name"
    printf '[General]\nuiScale=%s\n[debug]\nlevel=off\n[audio]\nmuted=true\n' "$scale" >"$DATA/$name/settings.ini"
    QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA/$name" QTCRAFT_SCREENSHOT="$DATA/$name.png" \
        QTCRAFT_SCREENSHOT_FRAMES=20 QTCRAFT_WINDOW_SIZE="$size" QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_ACCESSIBILITY_CHECK=1 \
        QTCRAFT_LOG_ITEMS=loadingTitle,loadingBar,startScreen,loadingStep4Count,loadingStep5Count \
        perl -e 'alarm shift; exec @ARGV or die' 120 "$APP" --no-audio --no-mods --screen builtin --background \
        --overlay "$overlay" "$@" >"$DATA/$name.log" 2>&1
    status=$?
    if [ "$status" -ne 0 ] || [ ! -s "$DATA/$name.png" ]; then
        echo "$name: game failed (exit $status) or took no screenshot:"
        tail -20 "$DATA/$name.log"
        failed=1
        return 1
    fi
    if grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: ' "$DATA/$name.log"; then
        echo "$name: QML reported problems"
        failed=1
    fi
    if ! grep -q 'Layout check: 0 items outside' "$DATA/$name.log" || ! grep -q 'Accessibility check: 0 controls' "$DATA/$name.log"; then
        echo "$name: layout or accessibility check failed:"
        grep -E 'Layout check|Accessibility check|outside|without' "$DATA/$name.log" | head -20
        failed=1
    fi
    return 0
}

# shown <name> <object name> <text>: the logged line of that item is visible and holds <text>.
shown() {
    line=$(grep "Item $2: " "$DATA/$1.log" | tail -1)
    if ! echo "$line" | grep -qF -- 'visible yes' || ! echo "$line" | grep -qF -- "$3"; then
        echo "$1: $2 is not shown with $3: $line"
        failed=1
    fi
}

# playable_once <name>: the world of run <name> became playable exactly once, without the longest wait.
playable_once() {
    count=$(grep -c 'World start: playable after' "$DATA/$1.log")
    if [ "$count" -ne 1 ]; then
        echo "$1: the world became playable $count times"
        failed=1
    fi
    if grep -q 'the world shows anyway' "$DATA/$1.log"; then
        echo "$1: the spawn area was not meshed in time"
        failed=1
    fi
}

# expect <name> <text>: the log of run <name> holds <text>.
expect() {
    if ! grep -qF -- "$2" "$DATA/$1.log"; then
        echo "$1: expected \"$2\" in the log:"
        grep -E 'Item |loading' "$DATA/$1.log" | head -20
        failed=1
    fi
}

for setup in "100 1024x640" "200 2048x1280"; do
    set -- $setup
    run "chunks-$1" "$1" "$2" "@0 loading:chunks,newworld,23,0,49" && {
        shown "chunks-$1" loadingTitle 'text "Creating the world"'
        shown "chunks-$1" loadingStep4Count 'text "23 / 49"'
    }
    run "meshes-$1" "$1" "$2" "@0 loading:meshes,savedworld,49,31,49" && {
        shown "meshes-$1" loadingTitle 'text "Loading the world"'
        shown "meshes-$1" loadingStep4Count 'text "49 / 49"'
        shown "meshes-$1" loadingStep5Count 'text "31 / 49"'
    }
    run "start-$1" "$1" "$2" "@0 starting" && shown "start-$1" startScreen 'width'
done

# Refused: no loading screen to show, counts that cannot be.
run refused-ready 100 1024x640 "@0 loading:ready" && expect refused-ready '--overlay loading:ready: expected a stage'
run refused-counts 100 1024x640 "@0 loading:meshes,newworld,10,11,49" && expect refused-counts '--overlay loading:meshes,newworld,10,11,49: expected'

# A refused notation leaves the menu and says why.
run refused 100 1024x640 "@0 loading:sideways" && expect refused '--overlay loading:sideways: expected a stage'

# A real start: playable once the spawn area is meshed, before the screenshot of the loaded world.
run play 100 1024x640 "" --play --world-type flat --view-distance 2 && playable_once play

# A state shown by the hook in a playing world stays on the screen; the world plays only once.
run play-shown 100 1024x640 "@300 loading:meshes,savedworld,49,31,49" --play --world-type flat --view-distance 2 && {
    playable_once play-shown
    shown play-shown loadingStep5Count 'text "31 / 49"'
}

# A saved world made by the headless bot, continued from the menu: the screenshot waits until it
# plays.
printf '%s\n' '{"cmd":"worldCreate","name":"Flach","generator":"flat"}' '{"cmd":"waitUntilReady"}' '{"cmd":"quit"}' >"$DATA/worlds.jsonl"
if ! perl -e 'alarm shift; exec @ARGV or die' 60 "$HEADLESS" --data-dir "$DATA/menu-start" --no-mods --view-distance 2 \
    --script "$DATA/worlds.jsonl" >"$DATA/worlds.log" 2>&1; then
    echo "worlds: the headless bot could not create the world:"
    tail -5 "$DATA/worlds.log"
    failed=1
fi
run menu-start 100 1024x640 "@300 click:continueButton" && {
    playable_once menu-start
    playable=$(grep -n 'World start: playable after' "$DATA/menu-start.log" | head -1 | cut -d: -f1)
    saved=$(grep -n 'Saved screenshot' "$DATA/menu-start.log" | head -1 | cut -d: -f1)
    if [ -z "$playable" ] || [ -z "$saved" ] || [ "$playable" -gt "$saved" ]; then
        echo "menu-start: the screenshot was taken before the world played"
        grep -E 'World start|Saved screenshot|click' "$DATA/menu-start.log"
        failed=1
    fi
}

[ "$failed" -eq 0 ] && echo "loading screens checked"
exit "$failed"
