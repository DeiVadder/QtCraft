pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The stonecutter's screen (SlotScreen, decision E-785): the stone blocks of the inventory it cuts
// on the left, what the chosen one becomes on the right, the inventory below. A click on a result
// cuts one block, a shift-click as many as the inventory holds and has room for; the server decides
// and sends the inventory back.
SlotScreen {
    id: screen

    readonly property var stonecutter: game.stonecutter

    panelContent: content

    // A result as a screen reader reads it (decision E-725): "2 × Stone Slab from 1 × Stone".
    function resultText(result: var): string {
        const input = stonecutter.inputs[stonecutter.selected];
        return qsTr("%1 × %2 from %3").arg(result.count).arg(result.label).arg(input ? input.label : "");
    }

    RowLayout {
        id: content

        anchors.centerIn: parent
        spacing: Ui.px(20)

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: Ui.px(6)

            Label {
                text: qsTr("Stone")
                color: Theme.heading
                font.pixelSize: Ui.px(18)
                font.bold: true
            }

            Label {
                objectName: "noStone"
                visible: screen.stonecutter.inputs.length === 0
                Layout.maximumWidth: Ui.px(180)
                wrapMode: Text.Wrap
                text: qsTr("No stone to cut in the inventory.")
                color: Theme.textSecondary
            }

            GridLayout {
                objectName: "stonecutterInputs"
                columns: 3
                rowSpacing: Ui.px(4)
                columnSpacing: Ui.px(4)

                Repeater {
                    model: screen.stonecutter.inputs

                    ItemSlot {
                        required property var modelData
                        required property int index

                        objectName: "stoneInput" + index
                        Accessible.name: qsTr("Cut %1").arg(modelData.label)
                        Accessible.selected: screen.stonecutter.selected === index
                        slotIcon: modelData.icon
                        slotLabel: modelData.label
                        slotCount: modelData.count
                        highlighted: screen.stonecutter.selected === index || inputArea.containsMouse

                        MouseArea {
                            id: inputArea

                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: screen.stonecutter.selected = parent.index
                        }
                    }
                }
            }
        }

        ColumnLayout {
            spacing: Ui.px(12)

            Label {
                text: qsTr("Stonecutter")
                color: Theme.text
                font.pixelSize: Ui.px(24)
                font.bold: true
            }

            GridLayout {
                objectName: "stonecutterResults"
                columns: 6
                rowSpacing: Ui.px(4)
                columnSpacing: Ui.px(4)

                Repeater {
                    model: screen.stonecutter.results

                    ItemSlot {
                        required property var modelData
                        required property int index

                        objectName: "stoneResult" + index
                        Accessible.name: screen.resultText(modelData)
                        slotIcon: modelData.icon
                        slotLabel: modelData.label
                        slotCount: modelData.count
                        highlighted: resultArea.containsMouse

                        MouseArea {
                            id: resultArea

                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: mouse => screen.stonecutter.cut(parent.index, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                        }
                    }
                }
            }

            PlayerSlots {
                owner: screen
            }

            Label {
                Layout.maximumWidth: Ui.px(9 * 54)
                wrapMode: Text.Wrap
                text: screen.game.gamepad.active ? qsTr("Choose a stone on the left. %1 on a result cuts one block, %2 cuts all of them. %3 closes.")
                      .arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.north).arg(screen.game.gamepad.pads.texts.east)
                      : qsTr("Choose a stone on the left. Click a result to cut one block, shift-click to cut all of them. %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
                color: Theme.textSecondary
            }
        }
    }
}
