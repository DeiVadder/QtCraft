#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Layout test of the main menu in the smallest window (1024 x 640, Main.qml's minimum size): the
# real game renders the menu offscreen, alone and with each of its screens open, and the layout
# check (QTCRAFT_LAYOUT_CHECK) lists every shown item that reaches outside the window, such as a
# Quit button pushed below the bottom edge by a form taller than the window. Any such item, a
# missing check line or a QML warning fails the test. The main menu's pages and screens are checked
# in German as well. Mode "pad" (test app_offscreen_menu_pad) runs the pad's walks instead: the
# settings' pages one after the other (each page's layout checked after its step), back entries and
# Y (decision E-925), the handbook, the character's draft, the start page with saved worlds and
# "New world" in reading order.
# Usage: test_menu_layout.sh <qtcraft executable> <qtcraft-headless executable> [layout|pad]
set -u
APP=$1
HEADLESS=$2
MODE=${3:-layout}
DATA=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-layouttest.XXXXXX") || exit 2
trap 'rm -rf "$DATA"' EXIT
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$DATA/settings.ini"
failed=0

# check <name> [qtcraft options...]: one offscreen run of the menu. MENU_WORLD_TYPE (default flat)
# is the type "New world" starts with, LOG_ITEMS the items to log (QTCRAFT_LOG_ITEMS), RUN_DATA
# another data directory, PAD_STEPS=1 checks the layout after each pad step too.
check() {
    name=$1
    shift
    QTCRAFT_WINDOW_SIZE=1024x640 QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_MENU_WORLD_TYPE=${MENU_WORLD_TYPE:-flat} \
        QTCRAFT_LOG_ITEMS=${LOG_ITEMS:-} QTCRAFT_CHECK_PAD_STEPS=${PAD_STEPS:-} \
        QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="${RUN_DATA:-$DATA}" QTCRAFT_SCREENSHOT="$DATA/$name.png" QTCRAFT_SCREENSHOT_FRAMES=20 \
        perl -e 'alarm shift; exec @ARGV or die' 60 "$APP" --no-audio --no-mods --screen builtin --background "$@" >"$DATA/$name.log" 2>&1
    status=$?

    if [ "$status" -ne 0 ] || [ ! -s "$DATA/$name.png" ]; then
        echo "$name: game failed (exit $status) or took no screenshot:"
        tail -20 "$DATA/$name.log"
        failed=1
    elif ! grep -q 'Layout check: 0 items outside the window 1024x640' "$DATA/$name.log" \
        || grep -q 'Layout check: [1-9]' "$DATA/$name.log"; then
        echo "$name: items outside the 1024 x 640 window:"
        grep -E 'Outside the window|Layout check|padFocus' "$DATA/$name.log"
        failed=1
    elif grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: ' "$DATA/$name.log"; then
        echo "$name: QML reported problems"
        failed=1
    else
        echo "$name: everything inside the window"
    fi
}

# last_focus <name>: the pad's focus after the run's last pad step.
last_focus() {
    grep 'padFocus' "$DATA/$1.log" | tail -1 | sed 's/^padFocus: //'
}
# item_field <run> <name> <field>: a field of the logged item (x, y, width, height).
item_field() {
    grep "Item $2: x" "$DATA/$1.log" | tail -1 | awk -v field="$3" '{ for (i = 1; i < NF; i++) if ($i == field) print $(i + 1) }'
}
# item_text <run> <name> <text|placeholder>: what a logged text field holds or shows when empty.
item_text() {
    grep "Item $2: x" "$DATA/$1.log" | tail -1 | sed -n "s/.* $3 \"\([^\"]*\)\".*/\1/p"
}
# world_seed <data directory>: the seed in the world.json of the one world there.
world_seed() {
    find "$1" -name world.json | head -1 | xargs grep -o '"seed": *"*-*[0-9]*' | sed 's/.*: *"*//'
}

if [ "$MODE" = layout ]; then
check menu
check newworld --overlay "@300 open:newWorldPage"
check worldoptions --overlay "@300 open:worldOptionsPage"
check settings --overlay "@300 open:menuSettingsScreen"
check mods --overlay "@300 open:modsScreen"
check character --overlay "@300 open:menuCharacterScreen"
check multiplayer --overlay multiplayer
# The main menu and its screens in German (decision E-852), first start without a world.
check menu-de --language de
check newworld-de --language de --overlay "@300 open:newWorldPage"
check worldoptions-de --language de --overlay "@300 open:worldOptionsPage"
# "New world" and the world options of a normal world with their map previews (decision E-930): in
# the smallest window one column, the map at the end below the game mode.
MENU_WORLD_TYPE=terrain
LOG_ITEMS=gameModeChoice,newWorldMap
check newworld-terrain-de --language de --overlay "@300 open:newWorldPage"
if [ "$(item_field newworld-terrain-de newWorldMap y)" -lt "$(item_field newworld-terrain-de gameModeChoice y)" ] 2>/dev/null \
    || [ -z "$(item_field newworld-terrain-de newWorldMap y)" ]; then
    echo "newworld-terrain-de: the map is not in one column below the game mode at 1024 x 640:"
    grep 'Item ' "$DATA/newworld-terrain-de.log"
    failed=1
fi
LOG_ITEMS=
check worldoptions-terrain-de --language de --overlay "@300 open:worldOptionsPage"
# The world gets the seed the field stands for (decision E-930): with the field empty the random
# seed it shows as its placeholder, after the die the seed the die wrote into it. Each in a data
# directory of its own (the other runs start without a world).
LOG_ITEMS=worldSeed
for run in seed-empty seed-die; do
    RUN_DATA="$DATA/$run"
    mkdir -p "$RUN_DATA"
    cp "$DATA/settings.ini" "$RUN_DATA/"
    if [ "$run" = seed-die ]; then
        check "$run-de" --language de --overlay "@300 open:newWorldPage" --overlay "@600 click:worldSeedRandom" \
            --overlay "@900 click:createWorldButton"
        expected=$(item_text "$run-de" worldSeed text)
    else
        check "$run-de" --language de --overlay "@300 open:newWorldPage" --overlay "@600 click:createWorldButton"
        expected=$(item_text "$run-de" worldSeed placeholder)
    fi
    seed=$(world_seed "$RUN_DATA")
    if [ -z "$expected" ] || [ "$seed" != "$expected" ]; then
        echo "$run-de: the world's seed ($seed) is not the one the field stood for ($expected):"
        grep -E 'Item worldSeed|no such control|Opened world' "$DATA/$run-de.log"
        failed=1
    fi
done
RUN_DATA=
LOG_ITEMS=
MENU_WORLD_TYPE=
check mods-de --language de --overlay "@300 open:modsScreen"
check character-de --language de --overlay "@300 open:menuCharacterScreen"
check multiplayer-de --language de --overlay multiplayer
# The handbook takes the side bar one level down (decision E-852, guide 8b).
check handbook-de --language de --handbook block:qtcraft:furnace
# During a join (to a server that takes the connection and never answers) the side bar is locked:
# choosing "Character" keeps the multiplayer page, whose Cancel stays usable.
perl -MIO::Socket::INET -e '$s = IO::Socket::INET->new(Listen => 5, LocalAddr => "127.0.0.1", LocalPort => 0) or die;
    open(my $f, ">", $ARGV[0]) or die; print $f $s->sockport; close $f; sleep 90' "$DATA/port" &
listener=$!
tries=0
while [ ! -s "$DATA/port" ] && [ "$tries" -lt 50 ]; do
    perl -e 'select undef, undef, undef, 0.1'
    tries=$((tries + 1))
done
check join-locked-de --language de --join "127.0.0.1:$(cat "$DATA/port")" --overlay multiplayer \
    --overlay "@600 click:menuEntry2" --overlay "@900 click:cancelJoinButton"
kill "$listener" 2>/dev/null
wait "$listener" 2>/dev/null
if grep -q 'click:cancelJoinButton.*no such control' "$DATA/join-locked-de.log"; then
    echo "join-locked-de: the side bar left the multiplayer page during the join:"
    grep 'no such control' "$DATA/join-locked-de.log"
    failed=1
fi
fi

if [ "$MODE" = pad ]; then
# Back from the settings (the pad's B) the side bar's "Settings" entry has the focus again.
check settings-back-de --language de --overlay "@300 open:menuSettingsScreen" --overlay "@600 pad:dpadUp" \
    --overlay "@900 pad:east"
if [ "$(last_focus settings-back-de)" != menuEntry5 ]; then
    echo "settings-back-de: back from the settings, the focus is not on the side bar's entry:"
    grep 'padFocus' "$DATA/settings-back-de.log"
    failed=1
fi
# The pad's A and Y on the first focus of the settings stay in the settings (ES-1): A acts on the
# first setting, Y resets only a page with defaults (the graphics page has none).
check settings-stay-de --language de --overlay "@300 open:menuSettingsScreen" --overlay "@600 pad:dpadUp" \
    --overlay "@900 pad:north" --overlay "@1200 pad:south"
if grep 'padFocus' "$DATA/settings-stay-de.log" | grep -qE 'padFocus: (sidebarBack|menuEntry|$)'; then
    echo "settings-stay-de: the pad's A or Y left the settings:"
    grep 'padFocus' "$DATA/settings-stay-de.log"
    failed=1
fi
# The pad's Y resets a page with defaults only after a second Y (decision E-925): on the controls
# page (two right shoulders on) one Y keeps a changed key and shows the question in the hints, two
# reset it; B then closes and stores the settings. The graphics page has no Y hint.
reset_run() {
    name=$1
    shift
    printf '[debug]\nlevel=off\n[audio]\nmuted=true\n[keys]\nforward=I\n' >"$DATA/settings.ini"
    check "$name" --language de --overlay "@300 open:menuSettingsScreen" --overlay "@600 pad:dpadUp" \
        --overlay "@600 pad:rightShoulder" --overlay "@600 pad:rightShoulder" "$@" --overlay "@600 pad:east"
}
reset_run settings-y-once-de --overlay "@600 pad:north" --overlay "@600 text:padHintResetAgain=again"
if ! grep -q '^forward=I$' "$DATA/settings.ini" || grep -q 'padHintResetAgain.*no such control' "$DATA/settings-y-once-de.log"; then
    echo "settings-y-once-de: one Y reset the keys or showed no question:"
    grep -E 'padFocus|no such control|^forward=' "$DATA/settings-y-once-de.log" "$DATA/settings.ini"
    failed=1
fi
reset_run settings-y-twice-de --overlay "@600 text:padHintReset=reset" --overlay "@600 pad:north" --overlay "@600 pad:north"
if ! grep -q '^forward=W$' "$DATA/settings.ini" || grep -q 'no such control' "$DATA/settings-y-twice-de.log"; then
    echo "settings-y-twice-de: Y Y did not reset the keys, or the controls page showed no Y hint:"
    grep -E 'padFocus|no such control|^forward=' "$DATA/settings-y-twice-de.log" "$DATA/settings.ini"
    failed=1
fi
printf '[debug]\nlevel=off\n[audio]\nmuted=true\n' >"$DATA/settings.ini"
check settings-no-y-de --language de --overlay "@300 open:menuSettingsScreen" --overlay "@600 pad:dpadUp" \
    --overlay "@600 text:padHintReset=reset"
if ! grep -q 'padHintReset.*no such control' "$DATA/settings-no-y-de.log"; then
    echo "settings-no-y-de: the graphics page shows a Y hint:"
    grep 'padHintReset' "$DATA/settings-no-y-de.log"
    failed=1
fi
# In the handbook the pad's right shoulder puts the focus on the first entry of the next category.
check handbook-pad-de --language de --handbook block:qtcraft:furnace --overlay "@600 pad:dpadUp" \
    --overlay "@600 pad:rightShoulder"
if ! last_focus handbook-pad-de | grep -q '^HandbookEntryRow:'; then
    echo "handbook-pad-de: after the right shoulder the focus is not on an entry:"
    grep 'padFocus' "$DATA/handbook-pad-de.log"
    failed=1
fi
# Back from the texture packs opened on the mods page, "Texture packs …" has the focus again.
check mods-back-de --language de --overlay "@300 open:modsScreen" --overlay "@600 click:texturePacksLink" \
    --overlay "@900 pad:dpadUp" --overlay "@1200 pad:east"
if [ "$(last_focus mods-back-de)" != texturePacksLink ]; then
    echo "mods-back-de: back from the texture packs, the focus is not on the mods page's button:"
    grep 'padFocus' "$DATA/mods-back-de.log"
    failed=1
fi
# Saving with the pad's A on "Save" disables the button; the focus moves on to "Random" in the
# editor, so the pad's B (Esc) still leaves the page.
check character-save-de --language de --overlay "@300 open:menuCharacterScreen" --overlay "@600 click:characterRandom" \
    --overlay "@900 pad:dpadUp" --overlay "@1200 focus:characterSave" --overlay "@1500 pad:south" --overlay "@1800 pad:east"
if [ "$(grep 'padFocus' "$DATA/character-save-de.log" | tail -2 | head -1)" != "padFocus: characterRandom" ]; then
    echo "character-save-de: after saving, the focus left the editor:"
    grep 'padFocus' "$DATA/character-save-de.log"
    failed=1
elif last_focus character-save-de | grep -q '^character'; then
    echo "character-save-de: after saving, the pad's B did not leave the page:"
    grep 'padFocus' "$DATA/character-save-de.log"
    failed=1
fi
# The character page edits a draft (decision E-852): after a change, the pad's B asks whether to
# save or discard it, with the focus on "Keep editing" (the first press shows the pad's focus).
# The prompt is modal for the whole window: the pad's left and up stay on its buttons and never
# reach the side bar.
check character-leave-de --language de --overlay "@300 open:menuCharacterScreen" --overlay "@600 click:characterRandom" \
    --overlay "@900 pad:dpadUp" --overlay "@1200 pad:east" --overlay "@1500 pad:dpadLeft" --overlay "@1800 pad:dpadLeft" \
    --overlay "@2100 pad:dpadUp" --overlay "@2400 pad:dpadLeft"
if ! grep -q 'padFocus: keepEditingButton' "$DATA/character-leave-de.log"; then
    echo "character-leave-de: leaving with unsaved changes did not ask:"
    grep 'padFocus' "$DATA/character-leave-de.log"
    failed=1
elif ! grep 'padFocus' "$DATA/character-leave-de.log" | tail -1 | grep -qE 'padFocus: (discardChangesButton|keepEditingButton|saveChangesButton)'; then
    echo "character-leave-de: the pad's focus left the prompt:"
    grep 'padFocus' "$DATA/character-leave-de.log"
    failed=1
fi
# With saved worlds (made by the headless bot) the start page offers to continue the one played
# last, and the pad's focus starts on "Continue" (the first press shows the focus).
printf '%s\n' '{"cmd":"worldCreate","name":"Eine Welt mit einem recht langen Namen","seed":42,"gameMode":"creative"}' \
    '{"cmd":"waitUntilReady"}' '{"cmd":"worldCreate","name":"Flach","generator":"flat"}' '{"cmd":"waitUntilReady"}' \
    '{"cmd":"quit"}' >"$DATA/worlds.jsonl"
if ! perl -e 'alarm shift; exec @ARGV or die' 60 "$HEADLESS" --data-dir "$DATA" --no-mods --view-distance 2 \
    --script "$DATA/worlds.jsonl" >"$DATA/worlds.log" 2>&1; then
    echo "worlds: the headless bot could not create the worlds:"
    tail -5 "$DATA/worlds.log"
    failed=1
fi
# Down from there reaches "New world …" and then walks the list of worlds row by row (HM-6 of
# docs/Menue-Pruefung.md), not along the rows' buttons; right from a row reaches its Play.
check menu-world-de --language de --overlay "@300 pad:dpadDown" --overlay "@300 pad:dpadDown" --overlay "@300 pad:dpadDown" \
    --overlay "@300 pad:dpadDown" --overlay "@300 pad:dpadRight"
if ! grep -q 'padFocus: continueButton' "$DATA/menu-world-de.log"; then
    echo "menu-world-de: the start page offers no world to continue:"
    grep 'padFocus' "$DATA/menu-world-de.log"
    failed=1
elif [ "$(grep 'padFocus' "$DATA/menu-world-de.log" | tail -4 | tr '\n' ' ')" != \
    "padFocus: newWorldButton padFocus: worldRow0 padFocus: worldRow1 padFocus: GameButton:Spielen " ]; then
    echo "menu-world-de: down does not walk the list of worlds row by row, or right misses Play:"
    grep 'padFocus' "$DATA/menu-world-de.log"
    failed=1
fi
# The pad's A on a row plays its world.
check world-play-de --language de --overlay "@300 pad:dpadDown" --overlay "@300 pad:dpadDown" --overlay "@300 pad:dpadDown" \
    --overlay "@300 pad:south"
if ! grep -q 'Opened world' "$DATA/world-play-de.log"; then
    echo "world-play-de: A on the world's row did not play it:"
    grep 'padFocus' "$DATA/world-play-de.log"
    failed=1
fi
# "New world" in one column (decision E-930): the pad walks it in reading order, name, type, seed,
# the die, game mode, keeping the inventory, the world options, and down from there reaches
# "Create and play".
MENU_WORLD_TYPE=terrain
check newworld-pad-de --language de --overlay "@300 open:newWorldPage" --overlay "@600 pad:dpadUp" --overlay "@600 pad:dpadDown" \
    --overlay "@600 pad:dpadDown" --overlay "@600 pad:dpadRight" --overlay "@600 pad:dpadDown" --overlay "@600 pad:dpadDown" \
    --overlay "@600 pad:dpadDown" --overlay "@600 pad:dpadDown"
MENU_WORLD_TYPE=
order=$(grep 'padFocus' "$DATA/newworld-pad-de.log" | sed 's/^padFocus: //' | tr '\n' ' ')
if [ "$order" != "worldName ThemedComboBox: worldSeed worldSeedRandom ThemedComboBox: GameSwitch: moreWorldOptions createWorldButton " ]; then
    echo "newworld-pad-de: the pad's focus does not walk the form in reading order: $order"
    failed=1
fi
# The settings in German (its texts are longer), all seven pages in one run: the screen opens with
# the focus on the first setting; the first press (up) shows the pad's focus there and does not
# move it onto the back entry (guide 10, ES-1 of docs/Menue-Pruefung.md). After each of the six
# right shoulder presses the focus is on the new page, never in the side bar, and the page's layout
# is checked (PAD_STEPS).
PAD_STEPS=1
check settings-pages-de --language de --overlay "@300 open:menuSettingsScreen" --overlay "@600 pad:dpadUp" \
    --overlay "@600 pad:rightShoulder" --overlay "@600 pad:rightShoulder" --overlay "@600 pad:rightShoulder" \
    --overlay "@600 pad:rightShoulder" --overlay "@600 pad:rightShoulder" --overlay "@600 pad:rightShoulder"
PAD_STEPS=
if [ "$(grep -c 'padFocus' "$DATA/settings-pages-de.log")" -ne 7 ] \
    || grep 'padFocus' "$DATA/settings-pages-de.log" | grep -qE 'padFocus: (sidebarBack|NavButton:|$)'; then
    echo "settings-pages-de: the pad's focus left a page (back entry, side bar or none) or a step is missing:"
    grep 'padFocus' "$DATA/settings-pages-de.log"
    failed=1
fi
fi
exit $failed
