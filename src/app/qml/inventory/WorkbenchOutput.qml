// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// The output of a workbench screen (anvil, smithing table; decision E-884): row `row` of the
// inventory model as a slot that only shows what the work would make; a button takes it, the
// server decides. `slotName` names the slot for tests.
Repeater {
    id: output

    required property SlotScreen owner
    required property int row
    property string slotName

    model: SlotRange {
        source: output.owner.game.player.inventory
        first: output.row
        count: 1
    }

    ItemSlot {
        required property string icon
        required property int count
        required property real durability
        required property bool empty
        required property string label
        objectName: output.slotName
        slotIcon: icon
        slotCount: count
        slotDurability: durability
        slotEmpty: empty
        slotLabel: label
        infoEnabled: false
    }
}
