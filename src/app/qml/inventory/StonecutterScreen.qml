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
    readonly property int inputRow: InventoryModel.MachineFirst
    readonly property int outputRow: InventoryModel.MachineOutput

    title: qsTr("Stonecutter")
    handbookTarget: "qtcraft:stonecutter"
    keyHints: [
        { keys: [qsTr("Click")], text: qsTr("take, put down") },
        { keys: [qsTr("Right click")], text: qsTr("one") },
        { keys: [qsTr("Shift-click")], text: qsTr("move across, take all outputs") }
    ]

    // A result as a screen reader reads it (decision E-725): "2 × Stone Slab".
    function resultText(result: var): string {
        return qsTr("%1 × %2").arg(result.count).arg(result.label);
    }

    RowLayout {
        spacing: Ui.space.l

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
            implicitWidth: Ui.size.stoneResults
            implicitHeight: Ui.size.stoneResultsHeight
            radius: Ui.radius
            color: Theme.slotBorder

            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Ui.space.s
                visible: screen.stonecutter.results.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Put stone into the slot on the left.")
                color: Theme.textSecondary
                font.pixelSize: Ui.type.help
            }

            ScrollView {
                anchors.fill: parent
                anchors.margins: Ui.space.xs
                contentWidth: availableWidth
                clip: true

                GridLayout {
                    columns: 4
                    rowSpacing: Ui.space.xs
                    columnSpacing: Ui.space.xs

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

        PixelArt {
            Layout.alignment: Qt.AlignVCenter
            picture: screen.stonecutter.selected >= 0 ? "arrow/text" : "arrow/textSecondary"
        }

        Repeater {
            model: SlotRange {
                source: screen.game.player.inventory
                first: screen.outputRow
                count: 1
            }

            InventorySlot {
                objectName: "stonecutterOutput"
                owner: screen
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
}
