#!/bin/sh
# Layout test of the main menu in the smallest window (1024 x 640, Main.qml's minimum size): the
# real game renders the menu offscreen, alone and with each of its screens open, and the layout
# check (QTCRAFT_LAYOUT_CHECK) lists every shown item that reaches outside the window, such as a
# Quit button pushed below the bottom edge by a form taller than the window. Any such item, a
# missing check line or a QML warning fails the test. The main menu's pages and screens and every
# page of the settings are checked in German as well, the start page also with a saved world.
# Usage: test_menu_layout.sh <qtcraft executable> <qtcraft-headless executable>
set -u
APP=$1
HEADLESS=$2
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
check mods-de --language de --overlay "@300 open:modsScreen"
check character-de --language de --overlay "@300 open:menuCharacterScreen"
check multiplayer-de --language de --overlay multiplayer
# The settings and the handbook take the side bar one level down (decision E-852, guide 8b); back
# from the settings (the pad's B) the side bar's "Settings" entry has the focus again.
check handbook-de --language de --handbook block:qtcraft:furnace
# last_focus <name>: the pad's focus after the run's last pad step.
last_focus() {
    grep 'padFocus' "$DATA/$1.log" | tail -1 | sed 's/^padFocus: //'
}
check settings-back-de --language de --overlay "@300 open:menuSettingsScreen" --overlay "@600 pad:dpadUp" \
    --overlay "@900 pad:east"
if [ "$(last_focus settings-back-de)" != menuEntry5 ]; then
    echo "settings-back-de: back from the settings, the focus is not on the side bar's entry:"
    grep 'padFocus' "$DATA/settings-back-de.log"
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
check menu-world-de --language de --overlay "@300 pad:dpadDown"
if ! grep -q 'padFocus: continueButton' "$DATA/menu-world-de.log"; then
    echo "menu-world-de: the start page offers no world to continue:"
    grep 'padFocus' "$DATA/menu-world-de.log"
    failed=1
fi

# settings_page <name> <presses>: the settings in German (its texts are longer) at the page the
# pad's right shoulder reaches after <presses> presses; the steps run after the screen opened. The
# window without a screen has no focus of its own: the first press (up) shows the pad's focus on
# the first setting, as the pad's first press does, and the shoulders then go to the settings.
settings_page() {
    page=$1
    presses=$2
    set -- --language de --overlay "@300 open:menuSettingsScreen" --overlay "@600 pad:dpadUp"
    while [ "$presses" -gt 0 ]; do
        set -- "$@" --overlay "@600 pad:rightShoulder"
        presses=$((presses - 1))
    done
    check "settings-de-$page" "$@"
}
settings_page graphics 0
settings_page audio 1
settings_page controls 2
settings_page controller 3
settings_page interface 4
settings_page texturepacks 5
settings_page advanced 6
exit $failed
