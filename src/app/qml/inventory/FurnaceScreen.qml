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

    readonly property int inputRow: 46
    readonly property int fuelRow: 47

    outputSlot: 48
    title: screen.game.player.machineLabel.length > 0 ? screen.game.player.machineLabel : qsTr("Furnace")
    handbookTarget: "qtcraft:furnace"

    // A slot's name beside it (IV-8 of docs/Menue-Pruefung.md).
    component SlotName: Label {
        color: Theme.textMuted
        font.pixelSize: Ui.type.help
    }

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
            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.inputRow
                    count: 1
                }

                InventorySlot {
                    owner: screen
                }
            }

            Item {
                implicitWidth: 1
            }
            // The flame shrinks as the current fuel burns down.
            Rectangle {
                objectName: "flame"
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Ui.size.flame
                implicitHeight: Ui.size.flame
                radius: Ui.radius
                color: Theme.slotBorder

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 2 * Ui.space.xs
                    height: (parent.height - Ui.space.xs) * screen.game.player.machineBurn
                    radius: Ui.radius
                    gradient: Gradient {
                        GradientStop {
                            position: 0
                            color: Theme.flameCore
                        }
                        GradientStop {
                            position: 1
                            color: Theme.flame
                        }
                    }
                }
            }

            SlotName {
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                text: qsTr("Fuel")
            }
            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.fuelRow
                    count: 1
                }

                InventorySlot {
                    owner: screen
                }
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

            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.outputSlot
                    count: 1
                }

                InventorySlot {
                    owner: screen
                    Layout.alignment: Qt.AlignHCenter
                }
            }
            SlotName {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Result")
            }
        }
    }
}
