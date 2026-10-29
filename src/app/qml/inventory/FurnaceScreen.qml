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
    panelContent: content

    ColumnLayout {
        id: content

        anchors.centerIn: parent
        spacing: Ui.px(12)

        Label {
            textFormat: Text.PlainText
            text: screen.game.player.machineLabel.length > 0 ? screen.game.player.machineLabel : qsTr("Furnace")
            color: Theme.text
            font.pixelSize: Ui.px(24)
            font.bold: true
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Ui.px(24)

            ColumnLayout {
                spacing: Ui.px(6)

                Repeater {
                    model: SlotRange {
                        source: screen.game.player.inventory
                        first: screen.inputRow
                        count: 1
                    }

                    InventorySlot {
                        owner: screen
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                // The flame shrinks as the current fuel burns down.
                Rectangle {
                    objectName: "flame"
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: Ui.px(26)
                    implicitHeight: Ui.px(26)
                    radius: Ui.px(4)
                    color: Theme.slotBorder

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - Ui.px(6)
                        height: (parent.height - Ui.px(4)) * screen.game.player.machineBurn
                        radius: Ui.px(3)
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

                Repeater {
                    model: SlotRange {
                        source: screen.game.player.inventory
                        first: screen.fuelRow
                        count: 1
                    }

                    InventorySlot {
                        owner: screen
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // The arrow fills while the current item is worked on.
            Item {
                objectName: "progressArrow"
                implicitWidth: Ui.px(72)
                implicitHeight: Ui.px(28)

                Rectangle {
                    id: shaft

                    anchors.verticalCenter: parent.verticalCenter
                    width: Ui.px(52)
                    height: Ui.px(10)
                    color: Theme.slotBorder

                    Rectangle {
                        width: parent.width * screen.game.player.machineProgress
                        height: parent.height
                        color: Theme.text
                    }
                }

                Label {
                    anchors.left: shaft.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▶"
                    color: screen.game.player.machineProgress > 0 ? Theme.text : Theme.textMuted
                    font.pixelSize: Ui.px(20)
                }
            }

            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.outputSlot
                    count: 1
                }

                InventorySlot {
                    owner: screen
                }
            }
        }

        PlayerSlots {
            owner: screen
        }

        Label {
            Layout.maximumWidth: Ui.px(9 * 54)
            wrapMode: Text.Wrap
            text: screen.game.gamepad.active ? qsTr("Put something to smelt on top and fuel below. %1: pick up and put down. %2: put down one. %3: move the stack across. %4 closes.")
                    .arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.west).arg(screen.game.gamepad.pads.texts.north).arg(screen.game.gamepad.pads.texts.east)
                  : qsTr("Put something to smelt on top and fuel below. Left click: pick up and put down. Right click: put down one. Shift-click: move the stack across. %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
            color: Theme.textSecondary
        }
    }
}
