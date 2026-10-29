// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// The name of the item in the selected hotbar slot (decision E-885), centred over the hotbar's
// status rows: shown when another slot is selected (wheel, number keys, pad) or another item comes
// into the selected slot, for showMs, then faded out over fadeMs, as in the reference game. An
// empty slot hides it at once. Sized with the HUD (Ui.hudPx).
HudLabel {
    id: name

    // The player's inventory model (QcInventoryModel): selectedLabel and selectedItemAnnounced.
    required property var inventory

    readonly property int showMs: 2000
    readonly property int fadeMs: 500

    objectName: "heldItemName"
    text: name.inventory.selectedLabel
    opacity: 0
    visible: opacity > 0

    SequentialAnimation {
        id: showing

        PropertyAction {
            target: name
            property: "opacity"
            value: 1
        }
        PauseAnimation {
            duration: name.showMs
        }
        NumberAnimation {
            target: name
            property: "opacity"
            to: 0
            duration: name.fadeMs
        }
    }

    Connections {
        target: name.inventory

        function onSelectedItemAnnounced() {
            if (name.inventory.selectedLabel === "") {
                showing.stop();
                name.opacity = 0;
            } else {
                showing.restart();
            }
        }
    }
}
