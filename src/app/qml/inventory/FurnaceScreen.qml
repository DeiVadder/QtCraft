// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Screen of an open machine (furnace, SlotScreen): input, fuel and output above the inventory.
// The output can only be taken; a shift-click sends smeltable items to the input and fuel below.
SlotScreen {
    id: screen

    readonly property int inputRow: InventoryModel.MachineFirst
    readonly property int fuelRow: InventoryModel.MachineFirst + 1
    readonly property int outputRow: InventoryModel.MachineOutput

    title: screen.game.player.machineLabel.length > 0 ? screen.game.player.machineLabel : qsTr("Furnace")
    // The furnace, smoker or blast furnace that is open.
    handbookTarget: screen.game.player.machineBlock

    RowLayout {
        spacing: Ui.space.xl

        GridLayout {
            columns: 2
            rowSpacing: Ui.space.s
            columnSpacing: Ui.space.s

            SlotName {
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                text: qsTr("To smelt")
            }
            MachineSlotRow {
                owner: screen
                first: screen.inputRow
                count: 1
            }

            Item {
                implicitWidth: 1
            }
            FuelFlame {
                Layout.alignment: Qt.AlignHCenter
                share: screen.game.player.machineBurn
            }

            SlotName {
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                text: qsTr("Fuel")
            }
            MachineSlotRow {
                owner: screen
                first: screen.fuelRow
                count: 1
            }
        }

        // The arrow fills while the current item is worked on.
        Item {
            objectName: "progressArrow"
            implicitWidth: shaft.width + head.implicitWidth
            implicitHeight: head.implicitHeight

            Rectangle {
                id: shaft

                anchors.verticalCenter: parent.verticalCenter
                width: Ui.size.progressArrow
                height: Ui.space.s
                color: Theme.slotBorder

                Rectangle {
                    width: parent.width * screen.game.player.machineProgress
                    height: parent.height
                    color: Theme.text
                }
            }

            Label {
                id: head

                anchors.left: shaft.right
                anchors.verticalCenter: parent.verticalCenter
                text: "▶"
                color: screen.game.player.machineProgress > 0 ? Theme.text : Theme.textMuted
                font.pixelSize: Ui.type.section
            }
        }

        ColumnLayout {
            spacing: Ui.space.xs

            MachineSlotRow {
                Layout.alignment: Qt.AlignHCenter
                owner: screen
                first: screen.outputRow
                count: 1
            }
            SlotName {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Result")
            }
        }
    }
}
