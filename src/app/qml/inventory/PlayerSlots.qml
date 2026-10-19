pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// The 36 inventory slots of a SlotScreen: storage rows 9..35 first, the hotbar 0..8 below with a
// gap.
GridLayout {
    id: grid

    required property SlotScreen owner

    columns: 9
    rowSpacing: 2
    columnSpacing: 2

    Repeater {
        model: SlotRange {
            source: grid.owner.game.player.inventory
            count: 36
        }

        InventorySlot {
            owner: grid.owner
            Layout.row: slot < 9 ? 4 : Math.floor((slot - 9) / 9)
            Layout.column: slot % 9
            Layout.topMargin: slot < 9 ? 12 : 0
        }
    }
}
