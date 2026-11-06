#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# The question before a world of an older generator version opens (decisions E-1013, E-1014),
# offscreen in German:
# - continuing a world of version 1 asks instead of playing: "Keep version 1" (where the focus
#   starts), "Switch", "Switch with transition" and its width shown; the world does not start;
# - "Keep version 1" with "Don't ask again" plays the world and stores the answer in world.json; the
#   next start plays it without asking;
# - "Switch with transition" stores a ring (E-1040) and plays the world;
# - "Switch" backs the world up, switches it to the current version and plays it;
# - the progress of a switch as its hook shows it (upgrade:<stage>,<share>), the focus on "Cancel";
#   while writing "Cancel" is disabled.
# The steps wait for the wall clock (the menu, the switch, the world's start), hence label realtime.
# Usage: test_generator_upgrade_dialog.sh <qtcraft executable> <qtcraft-headless executable> <data directory>
set -u
APP=$1
HEADLESS=$2
DATA=$3
[ -n "$DATA" ] || exit 2
rm -rf "$DATA" && mkdir -p "$DATA" || exit 2
failed=0

# run <name> <directory> [qtcraft options...]: one offscreen run in German at 1280 x 720.
run() {
    name=$1
    dir=$2
    shift 2
    QTCRAFT_WINDOW_SIZE=1280x720 QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$dir" \
        QTCRAFT_LOG_ITEMS=generatorKeepButton,generatorSwitchButton,generatorBlendButton,generatorBlendChunks,generatorCancelButton,generatorDontAsk \
        QTCRAFT_SCREENSHOT="$DATA/$name.png" QTCRAFT_SCREENSHOT_FRAMES=40 \
        perl -e 'alarm shift; exec @ARGV or die' 120 "$APP" --no-audio --no-mods --screen builtin --background --language de "$@" \
        >"$DATA/$name.log" 2>&1
    status=$?
    if [ "$status" -ne 0 ] || [ ! -s "$DATA/$name.png" ]; then
        echo "$name: game failed (exit $status) or took no screenshot:"
        tail -20 "$DATA/$name.log"
        failed=1
        return 1
    elif ! grep -q "Layout check: 0 items outside the window 1280x720" "$DATA/$name.log"; then
        echo "$name: items outside the window:"
        grep -E 'Outside the window|Layout check' "$DATA/$name.log"
        failed=1
        return 1
    elif grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: ' "$DATA/$name.log"; then
        echo "$name: QML reported problems"
        failed=1
        return 1
    fi
    return 0
}

# expect <name> <object name> <pattern>: the logged line of that item matches the pattern.
expect() {
    line=$(grep "Item $2: " "$DATA/$1.log" | tail -1)
    case $line in
    *$3*) ;;
    *)
        echo "$1: $2 is not as expected ($3): $line"
        failed=1
        ;;
    esac
}

# expect_switched <name> <data directory>: the overworld's version in world.json is 2 (the line of
# the block "generator" itself, not those of its dimensions) and the version map is written.
expect_switched() {
    if ! sed -n '/"generator": {/,$p' "$2/worlds/Alt/world.json" | grep -q '^        "version": 2,\{0,1\}$'; then
        echo "$1: the world was not switched:"
        sed -n '/"generator": {/,/^    }/p' "$2/worlds/Alt/world.json"
        failed=1
    fi
    if [ ! -s "$2/worlds/Alt/generatorversions.qcv" ]; then
        echo "$1: the switched world has no version map"
        failed=1
    fi
}

# fresh <name>: a copy of the saved world of version 1 for one run.
fresh() {
    rm -rf "$DATA/world-$1" && cp -R "$saved" "$DATA/world-$1" && echo "$DATA/world-$1"
}

# A saved terrain world of generator version 1, made by the headless bot.
saved="$DATA/saved"
mkdir -p "$saved" && printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$saved/settings.ini"
printf '%s\n' '{"cmd":"worldCreate","name":"Alt","seed":7,"generatorVersion":1}' '{"cmd":"waitUntilReady"}' '{"cmd":"quit"}' \
    >"$DATA/worlds.jsonl"
if ! perl -e 'alarm shift; exec @ARGV or die' 90 "$HEADLESS" --data-dir "$saved" --no-mods --view-distance 2 \
    --script "$DATA/worlds.jsonl" >"$DATA/worlds.log" 2>&1; then
    echo "worlds: the headless bot could not create the world:"
    tail -5 "$DATA/worlds.log"
    exit 1
fi

# Continuing the world asks first; the world does not start.
if run prompt "$(fresh prompt)" --overlay "@300 click:continueButton"; then
    expect prompt generatorKeepButton 'visible yes text "Version 1 behalten" focused'
    expect prompt generatorSwitchButton 'visible yes'
    expect prompt generatorBlendButton 'visible yes text "Mit Übergang umstellen"'
    expect prompt generatorBlendChunks 'visible yes'
    expect prompt generatorDontAsk 'visible yes text "Nicht mehr fragen"'
    expect prompt generatorCancelButton 'visible no'
    if grep -q 'World start: playable after' "$DATA/prompt.log"; then
        echo "prompt: the world started without an answer"
        failed=1
    fi
fi

# "Keep version 1" with "Don't ask again" plays and remembers the answer; the next start does not ask.
kept=$(fresh keep)
if run keep "$kept" --overlay "@300 click:continueButton" --overlay "@800 click:generatorDontAsk" \
    --overlay "@1100 click:generatorKeepButton" --overlay "@6000 "; then
    if ! grep -q '"keepGeneratorVersion": 2' "$kept/worlds/Alt/world.json"; then
        echo "keep: the answer was not stored:"
        grep -n keep "$kept/worlds/Alt/world.json"
        failed=1
    fi
    if ! grep -q 'World start: playable after' "$DATA/keep.log"; then
        echo "keep: the world did not start after keeping its version"
        failed=1
    fi
    if run keep-again "$kept" --overlay "@300 click:continueButton" --overlay "@6000 "; then
        expect keep-again generatorKeepButton 'visible no'
        if ! grep -q 'World start: playable after' "$DATA/keep-again.log"; then
            echo "keep-again: the world did not start without the question"
            failed=1
        fi
    fi
fi

# "Switch with transition" stores a ring around the explored land and plays the world. The hook
# holds the screenshot while a switch runs, then the world's start holds it.
blended=$(fresh blend)
if run blend "$blended" --overlay "@300 click:continueButton" --overlay "@800 click:generatorBlendButton" --overlay "@1000 "; then
    expect_switched blend "$blended"
    if [ -z "$(ls "$blended/worlds/Alt/regions" 2>/dev/null)" ]; then
        echo "blend: no ring in the regions of the world"
        failed=1
    fi
    if ! grep -q 'World start: playable after' "$DATA/blend.log"; then
        echo "blend: the world did not start after the switch"
        failed=1
    fi
fi

# "Switch" switches the world, backs it up and plays it.
switched=$(fresh switch)
if run switch "$switched" --overlay "@300 click:continueButton" --overlay "@800 click:generatorSwitchButton" --overlay "@6000 "; then
    expect_switched switch "$switched"
    if [ -z "$(ls "$switched/backups" 2>/dev/null | grep before-generator-2)" ]; then
        echo "switch: no backup before the switch:"
        ls "$switched/backups" 2>&1
        failed=1
    fi
    if ! grep -q 'World start: playable after' "$DATA/switch.log"; then
        echo "switch: the world did not start after the switch:"
        grep -iE 'generator|world start' "$DATA/switch.log" | tail -5
        failed=1
    fi
fi

# The progress of a switch as its hook shows it: surveying, 40 % done.
if run progress "$(fresh progress)" --overlay "@300 upgrade:1,0.4"; then
    expect progress generatorCancelButton 'visible yes text "Abbrechen" focused'
    expect progress generatorKeepButton 'visible no'
fi
# While writing, the switch cannot be cancelled.
if run writing "$(fresh writing)" --overlay "@300 upgrade:3,0.5"; then
    expect writing generatorCancelButton 'visible yes text "Abbrechen" disabled'
fi

[ "$failed" -eq 0 ] && echo "generator switch question as expected"
exit $failed
