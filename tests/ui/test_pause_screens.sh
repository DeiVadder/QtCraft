#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# The pause menu, the death screen and the sign in the real game (decision E-928), offscreen:
# - the pause menu's buttons in a saved single-player world (save, open to LAN and manage server
#   shown, the last one fills its row; the way out saves) and on a joined server without operator
#   rights (the world's group hidden; the way out disconnects), read from QTCRAFT_LOG_ITEMS;
# - the pause and death cards above the HUD's status rows (hotbar, hearts), which stay visible
#   behind them (decision E-927), at 200 % in 1280 x 720 (113 % for the screens) and in 2560 x 1440
#   (225 %);
# - the sign typed on the pad's on-screen keyboard and closed by the script: the typed line is sent
#   (the keyboard types with insert(), which emits no textEdited).
# The steps wait for the wall clock (the world's start, the sign's editor), hence label realtime.
# Usage: test_pause_screens.sh <qtcraft executable> <qtcraft-headless executable> <data directory>
set -u
APP=$1
HEADLESS=$2
DATA=$3
[ -n "$DATA" ] || exit 2
rm -rf "$DATA" && mkdir -p "$DATA" || exit 2
failed=0

# settings <directory> <scale>: a data directory with that UI scale.
settings() {
    mkdir -p "$1" && printf '[General]\nuiScale=%s\n[debug]\nlevel=off\n[audio]\nmuted=true\n' "$2" >"$1/settings.ini"
}

# run <name> <directory> <window> [qtcraft options...]: one offscreen run in German.
run() {
    name=$1
    dir=$2
    size=$3
    shift 3
    QTCRAFT_WINDOW_SIZE=$size QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$dir" \
        QTCRAFT_LOG_ITEMS=worldCard,hearts,hotbar,saveWorldButton,openToLanButton,manageServerButton,leaveGameButton,deleteWorldMessage \
        QTCRAFT_SCREENSHOT="$DATA/$name.png" QTCRAFT_SCREENSHOT_FRAMES=40 \
        perl -e 'alarm shift; exec @ARGV or die' 120 "$APP" --no-audio --no-mods --screen builtin --background --language de "$@" \
        >"$DATA/$name.log" 2>&1
    status=$?
    if [ "$status" -ne 0 ] || [ ! -s "$DATA/$name.png" ]; then
        echo "$name: game failed (exit $status) or took no screenshot:"
        tail -20 "$DATA/$name.log"
        failed=1
        return 1
    elif ! grep -q "Layout check: 0 items outside the window $size" "$DATA/$name.log"; then
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

# item <name> <object name>: the logged line of that item ("x .. y .. width .. height .. visible ..").
item() {
    grep "Item $2: " "$DATA/$1.log" | tail -1
}

# field <line> <word>: the number after <word> in an item's line.
field() {
    echo "$1" | sed -n "s/.* $2 \([-0-9.]*\).*/\1/p"
}

# card_above_status <name>: the card ends above the topmost status row shown.
card_above_status() {
    card=$(item "$1" worldCard)
    bottom=$(echo "$(field "$card" y) + $(field "$card" height)" | bc)
    for row in hearts hotbar; do
        line=$(item "$1" $row)
        case $line in
        *"visible yes"*)
            if [ "$(echo "$bottom > $(field "$line" y)" | bc)" -eq 1 ]; then
                echo "$1: the card (bottom $bottom) covers the $row: $line"
                failed=1
            fi
            ;;
        *)
            echo "$1: the $row is not shown behind the screen: $line"
            failed=1
            ;;
        esac
    done
}

# A saved world, made by the headless bot; the menu continues it, the pause menu opens with it.
saved="$DATA/saved"
settings "$saved" 200
printf '%s\n' '{"cmd":"worldCreate","name":"Flach","generator":"flat"}' '{"cmd":"waitUntilReady"}' '{"cmd":"quit"}' >"$DATA/worlds.jsonl"
if ! perl -e 'alarm shift; exec @ARGV or die' 60 "$HEADLESS" --data-dir "$saved" --no-mods --view-distance 2 \
    --script "$DATA/worlds.jsonl" >"$DATA/worlds.log" 2>&1; then
    echo "worlds: the headless bot could not create the world:"
    tail -5 "$DATA/worlds.log"
    exit 1
fi

# fresh <name>: a copy of the saved world for one run (a death would be saved with it).
fresh() {
    rm -rf "$DATA/world-$1" && cp -R "$saved" "$DATA/world-$1" && echo "$DATA/world-$1"
}

for size in 1280x720 2560x1440; do
    if run "saved-pause-$size" "$(fresh "pause-$size")" $size --overlay pause --overlay "@300 click:continueButton"; then
        name="saved-pause-$size"
        for button in saveWorldButton openToLanButton manageServerButton; do
            case $(item $name $button) in
            *"visible yes"*) ;;
            *)
                echo "$name: $button is not shown in a saved single-player world:"
                item $name $button
                failed=1
                ;;
            esac
        done
        save=$(field "$(item $name saveWorldButton)" width)
        manage=$(field "$(item $name manageServerButton)" width)
        if [ "$(echo "$manage < 1.5 * $save" | bc)" -eq 1 ]; then
            echo "$name: the last button of three does not fill its row (save $save, manage server $manage)"
            failed=1
        fi
        if ! item $name leaveGameButton | grep -q 'text "Speichern und zum Menü"'; then
            echo "$name: the way out does not save:"
            item $name leaveGameButton
            failed=1
        fi
        card_above_status $name
    fi
    if run "saved-death-$size" "$(fresh "death-$size")" $size --chat "/kill" --overlay "@300 click:continueButton"; then
        card_above_status "saved-death-$size"
    fi
done

# The question before deleting a world, opened by its hook in the main menu: without a world of its
# own it asks about the one played last (decision E-931).
if run delete-world "$(fresh delete)" 1280x720 --overlay "@300 open:deleteWorldDialog"; then
    if ! item delete-world deleteWorldMessage | grep -q '„Flach“'; then
        echo "delete-world: the question does not name the saved world:"
        item delete-world deleteWorldMessage
        failed=1
    fi
fi

# A joined server without operator rights: no save, LAN or manage server, the way out disconnects.
joined="$DATA/joined"
settings "$joined" 100
mkdir -p "$joined/server"
perl -e 'alarm shift; exec @ARGV or die' 120 "$HEADLESS" --server --port 0 --bind 127.0.0.1 --generator flat \
    --data-dir "$joined/server" --no-mods --no-log-file >"$DATA/server.log" 2>&1 &
server=$!
tries=0
while ! grep -q 'Listening on port' "$DATA/server.log" && [ "$tries" -lt 100 ]; do
    perl -e 'select undef, undef, undef, 0.1'
    tries=$((tries + 1))
done
port=$(sed -n 's/.*Listening on port \([0-9]*\).*/\1/p' "$DATA/server.log" | head -1)
if [ -z "$port" ]; then
    echo "joined: the dedicated server did not start:"
    tail -5 "$DATA/server.log"
    failed=1
elif run joined-pause "$joined" 1280x720 --join "127.0.0.1:$port" --player-name Gast --overlay pause; then
    for button in saveWorldButton openToLanButton manageServerButton; do
        case $(item joined-pause $button) in
        *"visible no"*) ;;
        *)
            echo "joined-pause: $button is shown on a joined server without operator rights:"
            item joined-pause $button
            failed=1
            ;;
        esac
    done
    if ! item joined-pause leaveGameButton | grep -q 'text "Verbindung trennen"'; then
        echo "joined-pause: the way out does not disconnect:"
        item joined-pause leaveGameButton
        failed=1
    fi
fi
kill "$server" 2>/dev/null
wait "$server" 2>/dev/null

# The sign typed on the pad's keyboard: the first A opens the keyboard on the first line, the
# second types its first key; the script then closes the screen, which sends the draft.
sign="$DATA/sign"
settings "$sign" 100
if run sign-keyboard "$sign" 1280x720 --play --world-type flat --view-distance 2 --look 0,-45 \
    --chat "/setblock ~ ~ ~-1 oak_sign" --overlay "@2000 key:use" --overlay "@2500 pad:south" --overlay "@2500 pad:south" \
    --overlay "@5000 "; then
    if ! grep -q 'padKeyboard: q' "$DATA/sign-keyboard.log" || ! grep -q 'Sign at .*: sending "q|||"' "$DATA/sign-keyboard.log"; then
        echo "sign-keyboard: the line typed on the pad's keyboard was not sent:"
        grep -E 'padKeyboard|Sign at' "$DATA/sign-keyboard.log"
        failed=1
    fi
fi

[ "$failed" -eq 0 ] && echo "pause menu, death screen and sign as expected"
exit $failed
