#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# UI scale and accessibility test (decision E-725): the real game renders every screen offscreen in
# a 1280 x 720 window at the scale given (settings.ini "uiScale": 100 or 200; 200 is limited to
# what fits, 112.5 % for the screens and 200 % for the HUD). The layout check (QTCRAFT_LAYOUT_CHECK)
# must find no item outside the window and the chat lines above the status rows; the accessibility
# check (QTCRAFT_ACCESSIBILITY_CHECK) must find no shown control without an accessible name or role.
# A missing check line or a QML warning fails the test too. No step waits for the wall clock: the
# screens open with the world (--overlay, --game-mode), the pad's steps run one after another and
# the screenshot waits for each to be reported (QcScriptedPad).
# Large windows (decision E-875): QC_UI_SIZE=<width>x<height> renders in that window instead, and
# QC_UI_EXPECT=<screens>/<hud> (percent) is the factor the game must log ("UI scale: screens <n> %,
# HUD <n> %") in every run, so no screen keeps the size of a small window.
# Usage: test_ui_scale.sh <qtcraft executable> <scale> <data directory> [directory for the screenshots]
# The data directory (build/<preset>/test-data/<test>, E-380) is emptied first and keeps the logs.
set -u
APP=$1
SCALE=$2
DATA=$3
[ -n "$DATA" ] || exit 2
rm -rf "$DATA" && mkdir -p "$DATA" || exit 2
SHOTS=${4:-$DATA}
SIZE=${QC_UI_SIZE:-1280x720}
EXPECT=${QC_UI_EXPECT:-}
printf '[General]\nuiScale=%s\n[debug]\nlevel=off\n[audio]\nmuted=true\n' "$SCALE" >"$DATA/settings.ini"
failed=0

# check <name> [qtcraft options...]: one offscreen run.
check() {
    name=$1
    shift
    QTCRAFT_WINDOW_SIZE=$SIZE QTCRAFT_LAYOUT_CHECK=1 QTCRAFT_ACCESSIBILITY_CHECK=1 QTCRAFT_MENU_WORLD_TYPE=${MENU_WORLD_TYPE:-flat} \
        QTCRAFT_LOG_ITEMS=${LOG_ITEMS:-} QTCRAFT_OFFSCREEN=1 QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$DATA" QTCRAFT_SCREENSHOT="$SHOTS/ui-$SCALE-$name.png" QTCRAFT_SCREENSHOT_FRAMES=40 \
        perl -e 'alarm shift; exec @ARGV or die' 90 "$APP" --no-audio --no-mods --screen builtin --background "$@" >"$DATA/$name.log" 2>&1
    status=$?

    if [ "$status" -ne 0 ] || [ ! -s "$SHOTS/ui-$SCALE-$name.png" ]; then
        echo "$name: game failed (exit $status) or took no screenshot:"
        tail -20 "$DATA/$name.log"
        failed=1
    elif ! grep -q "Layout check: 0 items outside the window $SIZE" "$DATA/$name.log"; then
        echo "$name: items outside the window:"
        grep -E 'Outside the window|Layout check' "$DATA/$name.log"
        failed=1
    elif grep -q 'HUD check: chat lines overlap [1-9]' "$DATA/$name.log"; then
        echo "$name: chat lines overlap the status rows:"
        grep -E 'Chat lines overlap|HUD check' "$DATA/$name.log"
        failed=1
    elif ! grep -q 'Accessibility check: 0 controls without a name' "$DATA/$name.log"; then
        echo "$name: controls without an accessible name or role:"
        grep -E 'No accessible name|Accessibility check' "$DATA/$name.log"
        failed=1
    elif grep -E '\.qml(:[0-9]+)?(:[0-9]+)?: |no block in view opened' "$DATA/$name.log"; then
        echo "$name: QML reported problems or the screen did not open"
        failed=1
    elif [ -n "$EXPECT" ] && ! grep 'UI scale: screens' "$DATA/$name.log" | tail -1 | grep -q "screens ${EXPECT%/*} %, HUD ${EXPECT#*/} %"; then
        echo "$name: UI scale is not ${EXPECT%/*} % / ${EXPECT#*/} % in the window $SIZE:"
        grep 'UI scale:' "$DATA/$name.log"
        failed=1
    else
        echo "$name: inside the window, every control named"
    fi
}

world="--play --world-type flat --view-distance 2"
# item_shown <run> <name>: whether a logged item is shown ("yes"/"no", empty when not logged).
item_shown() {
    grep "Item $2: x" "$DATA/$1.log" | tail -1 | sed -n 's/.* visible \([a-z]*\).*/\1/p'
}
# expect_shown <run> <name> <yes|no>: fails the test when the item's visibility differs.
expect_shown() {
    if [ "$(item_shown "$1" "$2")" != "$3" ]; then
        echo "$1: $2 is not shown as expected ($3): $(grep "Item $2:" "$DATA/$1.log" | tail -1)"
        failed=1
    fi
}
# The first start (no world yet) offers its links: handbook, multiplayer, character (HM-4).
LOG_ITEMS=firstStartLinks
check menu
LOG_ITEMS=
expect_shown menu firstStartLinks yes
check newworld --overlay "@300 open:newWorldPage"
check worldoptions --overlay "@300 open:worldOptionsPage"
# "New world" of a normal world (draft D.3 A, decision E-930): the game mode stands in the page's
# view without scrolling (its bottom above the form's), the map stands in its own column right of
# it, and the pad walks the form in reading order: name, type, seed, the die, game mode, keeping the
# inventory, the generator version, the world options, then down to "Create and play"; the world options page with its
# full map and sliders fits as well.
MENU_WORLD_TYPE=terrain
LOG_ITEMS=newWorldForm,gameModeChoice,newWorldMap
check newworld-terrain --overlay "@300 open:newWorldPage" --overlay "@600 pad:dpadUp" --overlay "@600 pad:dpadDown" \
    --overlay "@600 pad:dpadDown" --overlay "@600 pad:dpadRight" --overlay "@600 pad:dpadDown" --overlay "@600 pad:dpadDown" \
    --overlay "@600 pad:dpadDown" --overlay "@600 pad:dpadDown" --overlay "@600 pad:dpadDown"
LOG_ITEMS=
# item_bottom <run> <name>: the bottom edge of a logged item in the window.
item_bottom() {
    grep "Item $2: x" "$DATA/$1.log" | tail -1 | awk '{ for (i = 1; i < NF; i++) { if ($i == "y") y = $(i + 1); if ($i == "height") h = $(i + 1) } } END { print y + h }'
}
form_bottom=$(item_bottom newworld-terrain newWorldForm)
mode_bottom=$(item_bottom newworld-terrain gameModeChoice)
mode_right=$(grep "Item gameModeChoice: x" "$DATA/newworld-terrain.log" | tail -1 | awk '{ for (i = 1; i < NF; i++) { if ($i == "x") x = $(i + 1); if ($i == "width") w = $(i + 1) } } END { print x + w }')
map_left=$(grep "Item newWorldMap: x" "$DATA/newworld-terrain.log" | tail -1 | awk '{ for (i = 1; i < NF; i++) if ($i == "x") x = $(i + 1) } END { print x }')
if ! awk -v map="$map_left" -v mode="$mode_right" 'BEGIN { exit !(map != "" && map >= mode) }'; then
    echo "newworld-terrain: the map ($map_left) is not in a column right of the form ($mode_right)"
    failed=1
fi
if ! awk -v mode="$mode_bottom" -v form="$form_bottom" 'BEGIN { exit !(mode > 0 && mode <= form) }'; then
    echo "newworld-terrain: the game mode (bottom $mode_bottom) is not in the form's view (bottom $form_bottom) without scrolling"
    failed=1
fi
order=$(grep 'padFocus' "$DATA/newworld-terrain.log" | sed 's/^padFocus: //' | tr '\n' ' ')
if [ "$order" != "worldName ThemedComboBox: worldSeed worldSeedRandom ThemedComboBox: GameSwitch: ThemedComboBox: moreWorldOptions createWorldButton " ]; then
    echo "newworld-terrain: the pad's focus does not walk the form in reading order: $order"
    failed=1
fi
check worldoptions-terrain --overlay "@300 open:worldOptionsPage"
# The switch of the map's dimensions (decision E-1078): right from the form, right to the nether,
# confirmed, and the map shows the nether's scale.
LOG_ITEMS=worldMapScale
check newworld-dimension --overlay "@300 open:newWorldPage" --overlay "@600 pad:dpadUp" --overlay "@600 pad:dpadRight" \
    --overlay "@600 pad:dpadRight" --overlay "@600 pad:south"
LOG_ITEMS=
order=$(grep 'padFocus' "$DATA/newworld-dimension.log" | sed 's/^padFocus: //' | tr '\n' ' ')
if [ "$order" != "worldName worldMapDimension_qtcraft:overworld worldMapDimension_qtcraft:nether worldMapDimension_qtcraft:nether " ]; then
    echo "newworld-dimension: the pad's focus does not reach the switch right of the form: $order"
    failed=1
fi
expect_shown newworld-dimension worldMapScale yes
MENU_WORLD_TYPE=
check settings --overlay "@300 open:menuSettingsScreen"
check mods --overlay "@300 open:modsScreen"
check character --overlay "@300 open:menuCharacterScreen"
check multiplayer --overlay multiplayer
check menu-handbook --handbook block:qtcraft:furnace
# shellcheck disable=SC2086 # $world is a list of options
check hud $world --chat "/equip diamond" --chat "/xp add Player 5 levels" --chat "Hello" --chat "A chat line"
# shellcheck disable=SC2086
check inventory $world --chat "/give diamond_sword" --chat "/give stone 30" --overlay inventory
# shellcheck disable=SC2086
check creative $world --game-mode creative --overlay creative
# shellcheck disable=SC2086
check furnace $world --look 0,-89 --chat "/give raw_iron 5" --chat "/give coal 2" --chat "/setblock ~ ~-1 ~ furnace" --overlay furnace
# shellcheck disable=SC2086
check brewing $world --look 0,-89 --chat "/give sugar 5" --chat "/give ember_dust 2" --chat "/give awkward_potion" --chat "/give awkward_potion" --chat "/give awkward_potion" --chat "/setblock ~ ~-1 ~ brewing_stand" --overlay brewing
# shellcheck disable=SC2086
check chest $world --look 0,-89 --chat "/give stone 20" --chat "/setblock ~ ~-1 ~ chest" --overlay chest
# The machines in the common frame (stage 76.2-e): the key hints are shown and end inside it.
LOG_ITEMS=slotFrame,slotKeyHints
# shellcheck disable=SC2086
check anvil $world --look 0,-89 --chat "/give iron_sword" --chat "/setblock ~ ~-1 ~ anvil" --overlay anvil
LOG_ITEMS=
expect_shown anvil slotKeyHints yes
if [ "$(item_bottom anvil slotKeyHints)" -gt "$(item_bottom anvil slotFrame)" ] 2>/dev/null; then
    echo "anvil: the key hints end below the frame"
    failed=1
fi
# shellcheck disable=SC2086
check enchanting $world --look 0,-89 --chat "/give lapis_lazuli 5" --chat "/xp add Player 30 levels" \
    --chat "/setblock ~ ~-1 ~ enchanting_table" --overlay enchanting
# shellcheck disable=SC2086
check stonecutter $world --look 0,-89 --chat "/give stone 20" --chat "/setblock ~ ~-1 ~ stonecutter" --overlay "@2500 key:use"
# shellcheck disable=SC2086
check trade $world --look 0,-20 --chat "/summon villager 1 noai" --chat "/profession smith" --overlay trade
# shellcheck disable=SC2086
check pause $world --overlay pause
# shellcheck disable=SC2086
check pause-settings $world --overlay settings
# shellcheck disable=SC2086
check display $world --overlay display
# shellcheck disable=SC2086
check audio $world --overlay audio
# shellcheck disable=SC2086
check interface $world --overlay interface
# shellcheck disable=SC2086
check controls $world --overlay controls
# shellcheck disable=SC2086
check gamepad $world --overlay gamepad
# shellcheck disable=SC2086
check debug $world --overlay debug
# shellcheck disable=SC2086
check texturepacks $world --overlay texturepacks
# shellcheck disable=SC2086
check lan $world --overlay lan
# shellcheck disable=SC2086
check players $world --overlay players
# shellcheck disable=SC2086
check chat $world --overlay chat
# The handbook's facts with the technical ones folded away until "Technical" opens them, and its
# key hints instead of a help sentence (stage 76.2-f).
LOG_ITEMS=handbookFacts,handbookTechnicalFacts,handbookKeyHints
# shellcheck disable=SC2086
check handbook $world --handbook block:qtcraft:furnace
check handbook-technical --handbook block:qtcraft:furnace --overlay "@600 click:handbookTechnical"
LOG_ITEMS=
expect_shown handbook handbookFacts yes
expect_shown handbook handbookKeyHints yes
expect_shown handbook handbookTechnicalFacts no
expect_shown handbook-technical handbookTechnicalFacts yes
# shellcheck disable=SC2086
check death $world --chat "/kill"
# The sign's editor on ScreenFrame with its head (stage 76.2-d) and the message screen of the
# --overlay message: hook, here without a world.
# shellcheck disable=SC2086
check sign $world --overlay sign
check message --overlay "message:The world could not be loaded: its level.dat is damaged and no backup was found."
# The pause menu's card (draft D.1 A) on the pad, in German: the focus starts on "Continue" and
# moves down into the grid and across it.
# shellcheck disable=SC2086
check pad-pause $world --language de --overlay pause --overlay "@0 pad:dpadDown" --overlay "@0 pad:dpadDown" \
    --overlay "@0 pad:dpadRight"
if [ "$(grep -o 'padFocus: [A-Za-z]*' "$DATA/pad-pause.log" | sed 's/padFocus: //' | tr '\n' ' ')" != \
    "continueGameButton pauseSettingsButton pauseHandbookButton " ]; then
    echo "pad-pause: the pad's focus did not move through the pause menu as expected:"
    grep 'padFocus' "$DATA/pad-pause.log"
    failed=1
fi
# The advancement tree (decision E-924) and the details of a challenge beside it.
# shellcheck disable=SC2086
check advancements $world --overlay advancements \
    --overlay "@0 focus:advancement_qtcraft_diamond_armour"
if ! grep -q 'Advancement check: 0 overlapping advancements' "$DATA/advancements.log" || grep -q 'no such control' "$DATA/advancements.log"; then
    echo "advancements: advancements overlap or the tree did not show:"
    grep -E 'Advancements overlap|Advancement check' "$DATA/advancements.log"
    failed=1
fi
# The pad on the scaled inventory (decision E-583): its focus moves between the slots and the ring
# is drawn around them. The inventory opens with the world; the steps are queued at once and run in
# order, each after the one before is reported (QcScriptedPad), so none waits for the wall clock.
# shellcheck disable=SC2086
check pad-inventory $world --chat "/give stone 5" --overlay inventory --overlay "@0 pad:dpadDown" --overlay "@0 pad:dpadDown" \
    --overlay "@0 pad:dpadDown" --overlay "@0 pad:dpadRight" --overlay "@0 pad:south" --overlay "@0 pad:dpadUp"
if ! grep -o 'padFocus: slot:[0-9]*' "$DATA/pad-inventory.log" | tr '\n' ' ' | grep -Eq 'slot:100 .*slot:101 .*slot:102 .*slot:9'; then
    echo "pad-inventory: the pad's focus did not move as expected:"
    grep 'padFocus' "$DATA/pad-inventory.log"
    failed=1
fi
# The pad on the brewing stand's screen: its focus reaches the stand's slots (decision E-1063). The
# stand opens a moment after the world, so the steps wait; steps of one delay run in order.
# shellcheck disable=SC2086
check pad-brewing $world --look 0,-89 --chat "/setblock ~ ~-1 ~ brewing_stand" --overlay brewing --overlay "@4000 pad:dpadLeft" --overlay "@4000 pad:dpadUp" --overlay "@4000 pad:dpadRight" \
    --overlay "@4000 pad:dpadUp" --overlay "@4000 pad:dpadLeft" --overlay "@4000 pad:dpadLeft"
if ! grep -o 'padFocus: slot:[0-9]*' "$DATA/pad-brewing.log" | tr '\n' ' ' | grep -Eq 'slot:46 .*slot:50 .*slot:46 .*slot:48 .*slot:4[57]'; then
    echo "pad-brewing: the pad's focus did not reach the stand's slots:"
    grep 'padFocus' "$DATA/pad-brewing.log"
    failed=1
fi
# The chat on the pad: the on-screen keyboard (decision E-584) with its keys named.
# shellcheck disable=SC2086
check pad-keyboard $world --overlay chat --overlay "@0 pad:south" --overlay "@0 pad:south"
if ! grep -q 'padKeyboard: q' "$DATA/pad-keyboard.log"; then
    echo "pad-keyboard: the on-screen keyboard did not open or type"
    failed=1
fi
# In German the keyboard is QWERTZ (CT-2 of docs/Menue-Pruefung.md): five keys right of q is z.
# shellcheck disable=SC2086
check pad-keyboard-de $world --language de --overlay chat --overlay "@0 pad:south" --overlay "@0 pad:south" \
    --overlay "@0 pad:dpadRight" --overlay "@0 pad:dpadRight" --overlay "@0 pad:dpadRight" --overlay "@0 pad:dpadRight" \
    --overlay "@0 pad:dpadRight" --overlay "@0 pad:south"
if ! grep -q 'padKeyboard: qz$' "$DATA/pad-keyboard-de.log"; then
    echo "pad-keyboard-de: the German keyboard did not type q and z (QWERTZ):"
    grep 'padKeyboard' "$DATA/pad-keyboard-de.log"
    failed=1
fi
exit $failed
