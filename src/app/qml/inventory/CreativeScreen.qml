// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The creative inventory (SlotScreen, decision E-286): every item of the content, mods included,
// in categories (a list as in the handbook) and with a search on the left, the player's inventory
// with a bin above it on the right, and the switch to crafting and armour in the head. The search
// takes the keys only once clicked, so the inventory key still closes the screen. Taking from the catalogue never runs out; the server checks the mode.
SlotScreen {
    id: screen

    readonly property var catalogue: game.player.creativeItems

    // Columns of the catalogue: fewer in small windows, so the frame keeps its margin.
    readonly property int catalogueColumns: width < Ui.screen.narrowWindow ? 5 : 7

    title: qsTr("Creative items")
    creativeSwitch: true
    keyHints: [
        { keys: [qsTr("Click")], text: qsTr("a full stack") },
        { keys: [qsTr("Right click")], text: qsTr("one") },
        { keys: [qsTr("Shift-click")], text: qsTr("into the inventory") }
    ]

    // A click on the catalogue: left takes a full stack, right one item, shift-click puts a full
    // stack straight into the inventory. With a slot's stack picked up, it goes into the bin.
    function catalogueClicked(itemName, maxStack, button, quick) {
        if (pickedSlot >= 0) {
            game.player.clearSlot(pickedSlot);
            pickedSlot = -1;
            return;
        }
        if (quick) {
            game.player.putCreativeItem(-1, itemName, maxStack);
            return;
        }
        carriedItem = itemName;
        carriedCount = button === Qt.RightButton ? 1 : maxStack;
    }

    // The pad's shoulders switch the catalogue's categories (decision E-583).
    function gamepadCommand(command: int): bool {
        return categories.takeCommand(command);
    }

    function binClicked() {
        if (pickedSlot >= 0)
            game.player.clearSlot(pickedSlot);
        pickedSlot = -1;
        carriedItem = "";
    }

    startColumn: RowLayout {
        Layout.fillHeight: true
        spacing: Ui.space.s

        // The catalogue's categories, named as in the handbook (IV-7 of docs/Menue-Pruefung.md).
        NavList {
            id: categories

            objectName: "creativeTabs"
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Ui.size.creativeCategories
            entryName: "creativeTab"
            model: screen.catalogue.categoryNames
            // A click or the pad moves the list; the catalogue follows it.
            onCurrentIndexChanged: screen.catalogue.category = currentIndex
        }
        // The list shows the catalogue's category, also when it changes from elsewhere (a Binding:
        // the list's own writes would break a plain binding).
        Binding {
            target: categories
            property: "currentIndex"
            value: screen.catalogue.category
        }

        ColumnLayout {
            Layout.fillHeight: true
            spacing: Ui.space.s

            GameTextField {
                id: search

                objectName: "creativeSearch"
                Accessible.name: qsTr("Search items")
                Layout.fillWidth: true
                placeholderText: qsTr("Search items (name or id) …")
                onTextChanged: screen.catalogue.query = text
                Component.onCompleted: text = screen.catalogue.query
            }

            GridView {
                id: items

                objectName: "creativeItems"
                Accessible.name: qsTr("Items")
                // The columns and a gutter for the scroll bar.
                Layout.preferredWidth: screen.catalogueColumns * Ui.size.slotCell + Ui.size.scrollBar
                Layout.preferredHeight: 5 * Ui.size.slotCell
                Layout.fillHeight: true
                cellWidth: Ui.size.slotCell
                cellHeight: Ui.size.slotCell
                clip: true
                model: screen.catalogue
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThemedScrollBar {}

                delegate: ItemSlot {
                    id: entry

                    required property string itemName
                    required property string label
                    required property string icon
                    required property int maxStack

                    infoItem: itemName
                    infoEnabled: screen.pickedSlot < 0 && screen.carriedItem.length === 0
                    onInfoRequested: item => screen.game.handbook.open(item)
                    slotIcon: icon
                    slotLabel: label
                    slotEmpty: false
                    highlighted: hover.hovered

                    ThemedToolTip {
                        shown: hover.hovered && screen.pickedSlot < 0 && screen.carriedItem.length === 0
                        text: entry.label
                    }

                    HoverHandler {
                        id: hover

                        onHoveredChanged: screen.hoveredItem = hovered ? entry.itemName : ""
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        acceptedModifiers: Qt.NoModifier
                        onTapped: (eventPoint, button) => screen.catalogueClicked(entry.itemName, entry.maxStack, button, false)
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        acceptedModifiers: Qt.ShiftModifier
                        onTapped: (eventPoint, button) => screen.catalogueClicked(entry.itemName, entry.maxStack, button, true)
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: items.count === 0
                    text: qsTr("No item matches the search.")
                    color: Theme.textMuted
                    font.pixelSize: Ui.type.help
                }
            }

            Label {
                objectName: "creativeCount"
                text: qsTr("Items shown: %1").arg(screen.catalogue.count)
                color: Theme.textSecondary
                font.pixelSize: Ui.type.help
            }
        }
    }

    // The bin: a picked-up stack or the carried item disappears.
    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: Ui.space.s

        Label {
            text: qsTr("Bin")
            color: Theme.textMuted
            font.pixelSize: Ui.type.help
        }

        ItemSlot {
            objectName: "creativeBin"
            Accessible.name: qsTr("Bin")
            Accessible.description: qsTr("Bin: drop a stack here to delete it")
            slotEmpty: true
            highlighted: binHover.hovered
            infoEnabled: false

            Label {
                anchors.centerIn: parent
                text: "✕"
                color: Theme.textSecondary
                font.pixelSize: Ui.type.title
            }

            ThemedToolTip {
                shown: binHover.hovered
                text: qsTr("Bin: drop a stack here to delete it")
            }

            HoverHandler {
                id: binHover
            }

            TapHandler {
                onTapped: screen.binClicked()
            }
        }
    }
}
