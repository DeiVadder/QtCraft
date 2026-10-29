// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The stonecutter's screen (SlotScreen, decision E-785), laid out like the reference's: the input
// slot on the left, what its stone becomes in the middle, the output slot on the right and the
// inventory below. A click on a result chooses it; the output can only be taken (shift-click: as
// often as the input and the inventory's room allow). The slots are the inventory model's machine
// rows; the server decides and sends them back.
SlotScreen {
    id: screen

    readonly property var stonecutter: game.stonecutter
    readonly property int inputRow: 46

    outputSlot: 48
    panelContent: content

    // A result as a screen reader reads it (decision E-725): "2 × Stone Slab".
    function resultText(result: var): string {
        return qsTr("%1 × %2").arg(result.count).arg(result.label);
    }

    ColumnLayout {
        id: content

        anchors.centerIn: parent
        spacing: Ui.px(12)

        Label {
            text: qsTr("Stonecutter")
            color: Theme.text
            font.pixelSize: Ui.px(24)
            font.bold: true
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Ui.px(24)

            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.inputRow
                    count: 1
                }

                InventorySlot {
                    objectName: "stonecutterInput"
                    owner: screen
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            // The results of the input, four per row; empty without stone in the input.
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Ui.px(4 * 54) + Ui.px(12)
                implicitHeight: Ui.px(3 * 54) + Ui.px(12)
                radius: Ui.px(4)
                color: Theme.slotBorder

                Label {
                    objectName: "noResults"
                    anchors.centerIn: parent
                    width: parent.width - Ui.px(16)
                    visible: screen.stonecutter.results.length === 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Put stone into the slot on the left.")
                    color: Theme.textSecondary
                }

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: Ui.px(6)
                    contentWidth: availableWidth
                    clip: true

                    GridLayout {
                        objectName: "stonecutterResults"
                        columns: 4
                        rowSpacing: Ui.px(4)
                        columnSpacing: Ui.px(4)

                        Repeater {
                            model: screen.stonecutter.results

                            ItemSlot {
                                required property var modelData
                                required property int index

                                objectName: "stoneResult" + index
                                Accessible.name: screen.resultText(modelData)
                                Accessible.selected: screen.stonecutter.selected === index
                                slotIcon: modelData.icon
                                slotLabel: modelData.label
                                slotCount: modelData.count
                                slotEmpty: false
                                selected: screen.stonecutter.selected === index
                                highlighted: screen.stonecutter.selected === index || resultArea.containsMouse

                                MouseArea {
                                    id: resultArea

                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: screen.stonecutter.select(parent.index)
                                }
                            }
                        }
                    }
                }
            }

            Label {
                Layout.alignment: Qt.AlignVCenter
                text: "▶"
                color: screen.stonecutter.selected >= 0 ? Theme.text : Theme.textMuted
                font.pixelSize: Ui.px(20)
            }

            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.outputSlot
                    count: 1
                }

                InventorySlot {
                    objectName: "stonecutterOutput"
                    owner: screen
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }

        PlayerSlots {
            owner: screen
        }

        Label {
            Layout.maximumWidth: Ui.px(9 * 54)
            wrapMode: Text.Wrap
            text: screen.game.gamepad.active ? qsTr("Put stone into the left slot and choose a result. %1: pick up and put down, also the output. %2: put down one. %3: move the stack across or take all outputs. %4 closes.")
                    .arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.west).arg(screen.game.gamepad.pads.texts.north).arg(screen.game.gamepad.pads.texts.east)
                  : qsTr("Put stone into the left slot and choose a result. Left click: pick up and put down, also the output. Right click: put down one. Shift-click: move the stack across or take all outputs. %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
            color: Theme.textSecondary
        }
    }
}
