pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Inventory and crafting (SlotScreen): the 2 x 2 grid, or 3 x 3 at a crafting table, its result
// and the recipes the inventory can make right now. The result slot takes one crafted item, a
// shift-click as many as possible.
SlotScreen {
    id: screen

    readonly property int gridFirst: 36
    // Small windows (from the minimum 1024 x 640 on): a smaller preview and recipe list, so the
    // panel keeps its margin to the window's edges.
    readonly property bool narrow: width < 1200

    resultSlot: 45
    panelContent: content

    RowLayout {
        id: content

        anchors.centerIn: parent
        spacing: screen.narrow ? 16 : 28

        // The character as others see it, the armour slots (head to feet) beside it and the armour
        // points below them (decision E-234).
        RowLayout {
            Layout.alignment: Qt.AlignTop
            spacing: 8

            ColumnLayout {
                objectName: "equipmentColumn"
                Layout.alignment: Qt.AlignTop
                spacing: 2

                Repeater {
                    model: SlotRange {
                        source: screen.game.player.inventory
                        first: screen.game.player.equipmentFirstRow
                        count: 4
                    }

                    InventorySlot {
                        required property int index

                        owner: screen
                        // Icons exist once the content arrived.
                        placeholderIcon: screen.game.phase === GameController.Phase.Playing && screen.game.player.armourPlaceholders[index]
                                         ? "image://items/" + screen.game.player.armourPlaceholders[index] : ""
                    }
                }

                Label {
                    objectName: "armourPoints"
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Armour: %1").arg(screen.game.player.armour)
                    color: Theme.text
                    font.pixelSize: 15
                }
            }

            Rectangle {
                Layout.preferredWidth: screen.narrow ? 120 : 160
                Layout.preferredHeight: screen.narrow ? 200 : 260
                radius: 4
                color: Theme.panelRaised

                CharacterPreviewHost {
                    previewName: "inventoryPreview"
                    anchors.fill: parent
                    anchors.margins: 2
                    source: screen.game.preview
                    paused: screen.game.handbook.isOpen
                }
            }
        }

        ColumnLayout {
            spacing: 12

            RowLayout {
                spacing: 16

                // Creative mode: back to the item catalogue.
                Button {
                    objectName: "creativeItemsTab"
                    visible: screen.game.player.creative
                    text: qsTr("Creative items")
                    onClicked: screen.game.overlay = "creative"
                }

                Label {
                    Layout.fillWidth: true
                    text: screen.game.player.craftingSize === 3 ? qsTr("Crafting table") : qsTr("Inventory")
                    color: Theme.text
                    font.pixelSize: 24
                    font.bold: true
                }

                GridLayout {
                    columns: screen.game.player.craftingSize
                    rowSpacing: 2
                    columnSpacing: 2

                    Repeater {
                        model: SlotRange {
                            source: screen.game.player.inventory
                            first: screen.gridFirst
                            count: 9
                        }

                        InventorySlot {
                            readonly property int cell: slot - screen.gridFirst

                            owner: screen
                            visible: Math.floor(cell / 3) < screen.game.player.craftingSize
                                     && cell % 3 < screen.game.player.craftingSize
                        }
                    }
                }

                Label {
                    text: "→"
                    color: Theme.text
                    font.pixelSize: 28
                }

                Repeater {
                    model: SlotRange {
                        source: screen.game.player.inventory
                        first: screen.resultSlot
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
                Layout.maximumWidth: 9 * 54
                wrapMode: Text.Wrap
                text: qsTr("Left click: pick up and put down. Right click: put down one. Shift-click: move the stack across (on the result: craft as many as possible). %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
                color: Theme.textSecondary
            }
        }

        ColumnLayout {
            Layout.preferredWidth: screen.narrow ? 200 : 260
            Layout.fillHeight: true
            spacing: 6

            Label {
                text: qsTr("Can craft now")
                color: Theme.text
                font.pixelSize: 18
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 360
                clip: true
                spacing: 2
                model: screen.game.player.craftableRecipes

                Label {
                    anchors.centerIn: parent
                    visible: parent.count === 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Collect materials to see recipes here.")
                    color: Theme.textMuted
                }

                delegate: ItemDelegate {
                    id: recipeRow

                    required property var modelData

                    width: ListView.view.width
                    icon.source: modelData.icon
                    icon.color: Theme.clear
                    text: modelData.count > 1 ? qsTr("%1 × %2").arg(modelData.count).arg(modelData.label) : modelData.label
                    onClicked: screen.game.player.craftRecipe(recipeRow.modelData.recipe)
                }
            }
        }
    }
}
