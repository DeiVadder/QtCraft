// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtCraft

// The main menu's character page (variant C, decision E-852): the CharacterEditor in the page area
// beside the side bar, editing a draft that applies with "Save". The menu leaves the page through
// leave(), which asks first when there are unsaved changes.
ColumnLayout {
    id: page

    required property GameController game

    // The page should be shown (open(), also the test hook "@<ms> open:menuCharacterScreen").
    signal openRequested

    function open() {
        editor.begin();
        openRequested();
    }

    // Runs `proceed` once unsaved changes are saved or discarded (at once without any), then closes
    // the draft.
    function leave(proceed: var) {
        editor.leave(() => {
            editor.end();
            proceed();
        });
    }

    // The menu is gone (a world starts): the draft closes unapplied.
    function close() {
        editor.end();
    }

    spacing: 0

    CharacterEditor {
        id: editor

        Layout.fillWidth: true
        Layout.fillHeight: true
        game: page.game
    }
}
