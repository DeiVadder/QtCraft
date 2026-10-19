pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The creative inventory (SlotScreen, decision E-286): every item of the content, mods included,
// in tabs and with a search on the left, the player's inventory, a bin and the way to crafting and
// armour on the right. Taking from the catalogue never runs out; the server checks the mode.
SlotScreen {
    id: screen

    readonly property var catalogue: game.player.creativeItems

    // The search takes the keys only once clicked, so the inventory key still closes the screen.
    panelContent: content

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

    // The pad's shoulders switch the catalogue's tabs (decision E-583).
    function gamepadCommand(command: string): bool {
        if (command !== "tabPrevious" && command !== "tabNext")
            return false;
        const count = catalogue.categoryNames.length;
        if (count > 0)
            catalogue.category = (catalogue.category + (command === "tabNext" ? 1 : -1) + count) % count;
        return true;
    }

    function binClicked() {
        if (pickedSlot >= 0)
            game.player.clearSlot(pickedSlot);
        pickedSlot = -1;
        carriedItem = "";
    }

    RowLayout {
        id: content

        anchors.centerIn: parent
        spacing: 24

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 8 * 54
            spacing: 8

            Label {
                text: qsTr("Creative items")
                color: Theme.text
                font.pixelSize: 24
                font.bold: true
            }

            Flow {
                objectName: "creativeTabs"
                Layout.fillWidth: true
                spacing: 4

                Repeater {
                    model: screen.catalogue.categoryNames

                    Button {
                        required property string modelData
                        required property int index

                        text: modelData
                        checkable: true
                        checked: screen.catalogue.category === index
                        highlighted: checked
                        onClicked: screen.catalogue.category = index
                    }
                }
            }

            TextField {
                id: search

                objectName: "creativeSearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Search items (name or id) …")
                selectByMouse: true
                onTextChanged: screen.catalogue.query = text
                Component.onCompleted: text = screen.catalogue.query
            }

            GridView {
                id: items

                objectName: "creativeItems"
                // Eight columns and a gutter for the scroll bar.
                Layout.preferredWidth: 8 * 54 + 12
                Layout.preferredHeight: 6 * 54
                cellWidth: 54
                cellHeight: 54
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
                }
            }

            Label {
                objectName: "creativeCount"
                text: qsTr("Items shown: %1").arg(screen.catalogue.count)
                color: Theme.textSecondary
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: 12

            RowLayout {
                spacing: 12

                Button {
                    objectName: "creativeCrafting"
                    text: qsTr("Crafting and armour")
                    onClicked: screen.game.overlay = GameController.Overlay.Inventory
                }

                Item {
                    Layout.fillWidth: true
                }

                // The bin: a picked-up stack or the carried item disappears.
                ItemSlot {
                    objectName: "creativeBin"
                    slotEmpty: true
                    highlighted: binHover.hovered
                    infoEnabled: false

                    Label {
                        anchors.centerIn: parent
                        text: "✕"
                        color: Theme.textSecondary
                        font.pixelSize: 22
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

            PlayerSlots {
                owner: screen
            }

            Label {
                Layout.maximumWidth: 9 * 54
                wrapMode: Text.Wrap
                text: screen.game.gamepad.active ? qsTr("%1 on an item: a full stack, %2: one, %3: straight into the inventory. %1 on a slot puts it there. %4/%5 switch the tabs, %6 closes.")
                        .arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.west).arg(screen.game.gamepad.pads.texts.north)
                        .arg(screen.game.gamepad.pads.texts.leftShoulder).arg(screen.game.gamepad.pads.texts.rightShoulder).arg(screen.game.gamepad.pads.texts.east)
                      : qsTr("Left click on an item: a full stack, right click: one, shift-click: straight into the inventory. Click a slot to put it there. %1 or Esc closes (Esc while typing in the search).").arg(screen.game.settings.keys.labels.inventory)
                color: Theme.textSecondary
            }
        }
    }
}
