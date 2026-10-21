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
    rowSpacing: Ui.px(2)
    columnSpacing: Ui.px(2)

    Repeater {
        model: SlotRange {
            source: grid.owner.game.player.inventory
            count: grid.hotbarOnly ? 9 : 36
        }

        InventorySlot {
            owner: grid.owner
            Layout.row: slot < 9 ? 4 : Math.floor((slot - 9) / 9)
            Layout.column: slot % 9
            Layout.topMargin: slot < 9 && !grid.hotbarOnly ? Ui.px(12) : 0
        }
    }
}
