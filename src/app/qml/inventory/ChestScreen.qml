// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Screen of an open storage block (chest, barrel, hopper, dispenser, backpack; SlotScreen): its
// slots above the inventory, in rows as long as the block's data says. A backpack opened from the
// hand shows its row above the hotbar only, its own slot locked (decision E-833).
SlotScreen {
    id: screen

    readonly property int firstStorageRow: 46
    readonly property bool fromHand: screen.game.player.containerHeldSlot >= 0

    lockedSlot: screen.game.player.containerHeldSlot
    hotbarOnly: screen.fromHand
    title: screen.game.player.machineLabel.length > 0 ? screen.game.player.machineLabel : qsTr("Chest")
    keyHints: [
        { keys: [qsTr("Click")], text: qsTr("take, put down") },
        { keys: [qsTr("Right click")], text: qsTr("one") },
        { keys: [qsTr("Shift-click")], text: screen.fromHand ? qsTr("to the hotbar and back") : qsTr("move across") }
    ]

    GridLayout {
        objectName: "storage"
        columns: screen.game.player.containerColumns
        rowSpacing: Ui.size.slotGap
        columnSpacing: Ui.size.slotGap

        Repeater {
            model: SlotRange {
                source: screen.game.player.inventory
                first: screen.firstStorageRow
                count: screen.game.player.containerSlots
            }

            InventorySlot {
                readonly property int storageIndex: slot - screen.firstStorageRow

                owner: screen
                Layout.row: Math.floor(storageIndex / screen.game.player.containerColumns)
                Layout.column: storageIndex % screen.game.player.containerColumns
            }
        }
    }
}
