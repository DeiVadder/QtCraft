// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Screen of an open brewing stand (SlotScreen, decision E-1063): the ingredient above three bottle
// slots, the fuel beside them. A shift-click sends fuel, then ingredients and potions to their
// slots; the server decides what a slot takes.
SlotScreen {
    id: screen

    readonly property int ingredientRow: InventoryModel.MachineFirst
    readonly property int fuelRow: InventoryModel.MachineFirst + 1
    readonly property int firstBottleRow: InventoryModel.MachineOutput

    title: screen.game.player.machineLabel.length > 0 ? screen.game.player.machineLabel : qsTr("Brewing stand")
    handbookTarget: screen.game.player.machineBlock

    RowLayout {
        spacing: Ui.space.xl

        ColumnLayout {
            spacing: Ui.space.xs

            SlotName {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Fuel")
            }
            MachineSlotRow {
                Layout.alignment: Qt.AlignHCenter
                owner: screen
                first: screen.fuelRow
                count: 1
            }
            // How many brews the fuel in the stand still lasts.
            FuelFlame {
                Layout.alignment: Qt.AlignHCenter
                share: screen.game.player.machineBurn
            }
        }

        ColumnLayout {
            spacing: Ui.space.s

            SlotName {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Ingredient")
            }
            MachineSlotRow {
                Layout.alignment: Qt.AlignHCenter
                owner: screen
                first: screen.ingredientRow
                count: 1
            }

            // The bar fills while the brew runs.
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Ui.size.progressArrow
                implicitHeight: Ui.space.s
                color: Theme.slotBorder

                Rectangle {
                    width: parent.width * screen.game.player.machineProgress
                    height: parent.height
                    color: Theme.text
                }
            }

            MachineSlotRow {
                Layout.alignment: Qt.AlignHCenter
                owner: screen
                first: screen.firstBottleRow
                count: 3
            }
            SlotName {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Bottles")
            }
        }
    }
}
