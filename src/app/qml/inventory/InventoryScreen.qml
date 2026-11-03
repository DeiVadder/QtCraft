// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Inventory and crafting (SlotScreen): the character with its armour on the left, the 2 x 2 grid,
// or 3 x 3 at a crafting table, with its result above the inventory, and the recipes the inventory
// can make right now on the right. The result slot takes one crafted item, a
// shift-click as many as possible.
SlotScreen {
    id: screen

    readonly property int gridFirst: InventoryModel.GridFirst
    // Small windows (from the minimum 1024 x 640 on): a smaller preview and recipe list, so the
    // panel keeps its margin to the window's edges.
    readonly property bool narrow: width < Ui.screen.narrowWindow

    title: screen.game.player.craftingSize === 3 ? qsTr("Crafting table") : qsTr("Crafting")
    handbookTarget: screen.game.player.craftingHandbookTarget
    creativeSwitch: true
    keyHints: [
        { keys: [qsTr("Click")], text: qsTr("take, put down") },
        { keys: [qsTr("Right click")], text: qsTr("one") },
        { keys: [qsTr("Shift-click")], text: qsTr("move across, on the result: craft all") }
    ]

    // The character as others see it with the armour slots (head to feet) beside it and the armour
    // points as a plate on the figure (decision E-234, IV-9 of docs/Menue-Pruefung.md).
    startColumn: RowLayout {
        spacing: Ui.space.s

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: Ui.size.slotGap

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
                                     ? ItemIcons.source(screen.game.player.armourPlaceholders[index], ItemIcons.generation) : ""
                }
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: screen.narrow ? Ui.size.inventoryFigureSmall : Ui.size.inventoryFigure
            Layout.preferredHeight: Layout.preferredWidth * 1.6
            radius: Ui.radius
            color: Theme.panelRaised

            CharacterPreviewHost {
                previewName: "inventoryPreview"
                anchors.fill: parent
                anchors.margins: Ui.size.bevelDeep
                source: screen.game.preview
                paused: screen.game.handbook.isOpen
            }

            Rectangle {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: Ui.space.xs
                implicitWidth: armourLabel.implicitWidth + 2 * Ui.space.s
                implicitHeight: Ui.size.badge + Ui.space.xs
                radius: Ui.radius
                color: Theme.hudBackdrop

                Label {
                    id: armourLabel

                    objectName: "armourPoints"
                    anchors.centerIn: parent
                    text: qsTr("Armour: %1").arg(screen.game.player.armour)
                    color: Theme.text
                    font.pixelSize: Ui.type.help
                }
            }
        }
    }

    // The crafting grid, 2 x 2 or 3 x 3 at a crafting table, and its result.
    RowLayout {
        spacing: Ui.space.l

        GridLayout {
            columns: screen.game.player.craftingSize
            rowSpacing: Ui.size.slotGap
            columnSpacing: Ui.size.slotGap

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

        PixelArt {
            Layout.alignment: Qt.AlignVCenter
            picture: "arrow/textSecondary"
        }

        Repeater {
            model: SlotRange {
                source: screen.game.player.inventory
                first: InventoryModel.Result
                count: 1
            }

            InventorySlot {
                owner: screen
            }
        }
    }

    endColumn: ColumnLayout {
        Layout.preferredWidth: screen.narrow ? Ui.size.recipesSmall : Ui.size.recipes
        Layout.fillHeight: true
        spacing: Ui.space.s

        Label {
            text: qsTr("Can craft now")
            color: Theme.heading
            font.pixelSize: Ui.type.group
            font.bold: true
        }

        ListView {
            objectName: "craftableRecipes"
            Accessible.name: qsTr("Can craft now")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Ui.size.listMinimum
            clip: true
            spacing: Ui.size.line
            model: screen.game.player.craftableRecipes
            ScrollBar.vertical: ThemedScrollBar {}

            Label {
                anchors.centerIn: parent
                visible: parent.count === 0
                width: parent.width
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Collect materials to see recipes here.")
                color: Theme.textMuted
                font.pixelSize: Ui.type.help
            }

            delegate: ItemDelegate {
                id: recipeRow

                required property var modelData

                width: ListView.view.width
                icon.source: modelData.icon
                icon.color: Theme.clear
                icon.width: Ui.size.handbookIcon
                icon.height: Ui.size.handbookIcon
                font.pixelSize: Ui.type.body
                text: modelData.count > 1 ? qsTr("%1 × %2").arg(modelData.count).arg(modelData.label) : modelData.label
                onClicked: screen.game.player.craftRecipe(recipeRow.modelData.recipe)
            }
        }
    }
}
