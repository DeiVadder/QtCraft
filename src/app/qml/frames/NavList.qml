// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The category list on the left of a screen with several pages (decision E-845,
// docs/UI-Leitfaden.md 6.11): the chosen category on the raised surface with an accent bar on the
// left. A click chooses one; Ctrl+Tab (its shortcuts) and the pad's shoulders (takeCommand(), from
// the screen's gamepadCommand()) switch it. Up and down move the focus along the entries (and on
// out of the list, as Tab does), as in the main menu's side bar.
ColumnLayout {
    id: nav

    // The categories' names, in order.
    property list<string> model
    // The chosen category; -1 for none (the handbook while it shows search results).
    property int currentIndex
    // A short muted text at the right of each category (the handbook's counts); empty for none.
    property list<string> notes
    // Names the entries "<entryName><index>" for the test hooks ("click:adminSection1"); the
    // entries stay unnamed without it (the pad's focus is then logged as "NavButton:<text>").
    property string entryName

    // An entry took the focus (a list in a Flickable scrolls it into view).
    signal entryFocused(Item entry)
    // Ctrl+Tab or the pad's shoulders switched the category (the screen moves the focus onto the
    // new page, docs/UI-Leitfaden.md 10).
    signal switched

    // Moves the focus to the item before or after `item` in the focus chain.
    function moveFocus(item: Item, forward: bool) {
        item.nextItemInFocusChain(forward)?.forceActiveFocus(Qt.TabFocusReason);
    }

    // The entry of the chosen category, null for none.
    function currentEntry(): Item {
        return entryRepeater.itemAt(currentIndex);
    }

    // The category after (step 1) or before (step -1) the current one, wrapping around.
    function next(step: int) {
        if (model.length === 0)
            return;

        currentIndex = (currentIndex + step + model.length) % model.length;
        switched();
    }

    // The pad's shoulders (gamepadCommand "tabNext", "tabPrevious") switch the category: true if
    // `command` was one of them.
    function takeCommand(command: string): bool {
        if (command !== "tabNext" && command !== "tabPrevious")
            return false;

        next(command === "tabNext" ? 1 : -1);
        return true;
    }

    spacing: 0

    // Ctrl+Tab and Ctrl+Shift+Tab switch the category while the list is shown, wherever the focus
    // is on its screen: shortcuts come before the keys, and a control with the focus would take a
    // Tab with Meta to move the focus. On a Mac Qt names the Control key Meta (and Cmd Ctrl;
    // Cmd+Tab belongs to the system), so both count.
    Shortcut {
        sequences: ["Ctrl+Tab", "Meta+Tab"]
        enabled: nav.visible
        onActivated: nav.next(1)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Tab", "Meta+Shift+Tab"]
        enabled: nav.visible
        onActivated: nav.next(-1)
    }

    Repeater {
        id: entryRepeater

        model: nav.model

        delegate: NavButton {
            id: entry

            required property string modelData
            required property int index

            objectName: nav.entryName.length > 0 ? nav.entryName + index : ""
            page: true
            checked: index === nav.currentIndex
            text: modelData
            note: index < nav.notes.length ? nav.notes[index] : ""
            onClicked: nav.currentIndex = index
            onActiveFocusChanged: {
                if (activeFocus)
                    nav.entryFocused(entry);
            }
            Keys.onUpPressed: nav.moveFocus(entry, false)
            Keys.onDownPressed: nav.moveFocus(entry, true)
        }
    }
}
