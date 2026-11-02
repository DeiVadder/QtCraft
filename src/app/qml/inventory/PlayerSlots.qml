// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// The 36 inventory slots of a SlotScreen: storage rows 9..35 first, the hotbar 0..8 below with a
// gap; with `hotbarOnly` (a backpack opened from the hand, decision E-833) only the hotbar.
GridLayout {
    id: grid

    required property SlotScreen owner
    property bool hotbarOnly

    columns: 9
    rowSpacing: Ui.size.slotGap
    columnSpacing: Ui.size.slotGap

    Repeater {
        model: SlotRange {
            source: grid.owner.game.player.inventory
            count: grid.hotbarOnly ? InventoryModel.HotbarEnd : InventoryModel.InventoryEnd
        }

        InventorySlot {
            owner: grid.owner
            Layout.row: slot < InventoryModel.HotbarEnd ? 4 : Math.floor((slot - InventoryModel.HotbarEnd) / grid.columns)
            Layout.column: slot % grid.columns
            Layout.topMargin: slot < InventoryModel.HotbarEnd && !grid.hotbarOnly ? Ui.space.m : 0
        }
    }
}
