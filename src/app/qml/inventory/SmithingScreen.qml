// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The smithing table's screen (SlotScreen, decision E-1115), laid out like the reference's: the
// template, base and addition slots, an arrow and the output in one row, a line on what goes
// where and the button that takes the output (it goes into the base slot), and the inventory
// below. Built from the workbench parts of the anvil (MachineSlotRow, PixelArt, WorkbenchOutput).
// The slots are the inventory model's machine rows; the server decides and sends them back.
SlotScreen {
    id: screen

    readonly property var smithing: game.smithing
    readonly property int templateRow: InventoryModel.MachineFirst

    title: qsTr("Smithing Table")
    handbookTarget: screen.smithing.handbookTarget

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Ui.space.m

        MachineSlotRow {
            owner: screen
            first: screen.templateRow
            names: ["smithingTemplate", "smithingBase", "smithingAddition"]
            placeholders: [screen.smithing.templateItem]
        }

        PixelArt {
            Layout.alignment: Qt.AlignVCenter
            picture: "arrow/textSecondary"
        }

        WorkbenchOutput {
            owner: screen
            row: screen.templateRow + 3
            slotName: "smithingOutput"
        }
    }

    // What goes where (IV-8 of docs/Menue-Pruefung.md); the rest is in the handbook.
    Label {
        Layout.alignment: Qt.AlignHCenter
        text: qsTr("Template · diamond tool or armour · netherite ingot")
        color: Theme.textMuted
        font.pixelSize: Ui.type.help
    }

    GameButton {
        Layout.alignment: Qt.AlignHCenter
        objectName: "smithingForge"
        enabled: screen.smithing.hasOutput
        text: qsTr("Upgrade")
        onClicked: screen.smithing.forge()
    }
}
