pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Inventory and crafting. Left click picks a stack up and puts it down elsewhere (merging or
// swapping); right click puts down a single item. The result slot takes one crafted item.
Rectangle {
    id: screen

    required property GameController game
    property int pickedSlot: -1
    // Item under the mouse; F1 opens its handbook page.
    property string hoveredItem

    readonly property int gridFirst: 36
    readonly property int resultSlot: 45

    color: "#90000000"
    focus: visible
    onVisibleChanged: {
        pickedSlot = -1;
        if (visible)
            forceActiveFocus();
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_E) {
            screen.game.overlay = "";
            event.accepted = true;
        } else if (event.key === Qt.Key_F1) {
            screen.game.openHandbook(screen.hoveredItem);
            event.accepted = true;
        }
    }

    function clicked(slot, empty, button) {
        if (slot === resultSlot) {
            if (!empty)
                game.takeCraftingResult();
            return;
        }
        if (pickedSlot < 0) {
            if (!empty)
                pickedSlot = slot;
            return;
        }
        if (pickedSlot !== slot)
            game.moveItem(pickedSlot, slot, button === Qt.RightButton ? 1 : 0);
        // Right click keeps the stack picked up so several cells can be filled one by one.
        if (button !== Qt.RightButton)
            pickedSlot = -1;
    }

    MouseArea {
        anchors.fill: parent
        onClicked: screen.pickedSlot = -1
    }

    // Opaque panel so the crosshair and the world do not show through the slots.
    Rectangle {
        anchors.fill: content
        anchors.margins: -20
        radius: 6
        color: "#e8282828"
        border.color: "#80ffffff"
    }

    component Slot: ItemSlot {
        id: slotItem

        required property int slot
        required property string icon
        required property int count
        required property real durability
        required property bool empty
        required property string label
        required property string itemName

        infoItem: itemName
        infoEnabled: screen.pickedSlot < 0
        onInfoRequested: item => screen.game.openHandbook(item)
        slotIcon: icon
        slotCount: count
        slotDurability: durability
        slotEmpty: empty
        selected: slot === screen.pickedSlot
        highlighted: hover.hovered

        ToolTip.visible: hover.hovered && !empty
        ToolTip.text: label

        HoverHandler {
            id: hover

            onHoveredChanged: screen.hoveredItem = hovered && !slotItem.empty ? slotItem.itemName : ""
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onTapped: (eventPoint, button) => screen.clicked(slotItem.slot, slotItem.empty, button)
        }
    }

    RowLayout {
        id: content

        anchors.centerIn: parent
        spacing: 28

        ColumnLayout {
            spacing: 12

            RowLayout {
                spacing: 16

                Label {
                    Layout.fillWidth: true
                    text: screen.game.craftingSize === 3 ? qsTr("Crafting table") : qsTr("Inventory")
                    color: "white"
                    font.pixelSize: 24
                    font.bold: true
                }

                GridLayout {
                    columns: screen.game.craftingSize
                    rowSpacing: 2
                    columnSpacing: 2

                    Repeater {
                        model: SlotRange {
                            source: screen.game.inventory
                            first: screen.gridFirst
                            count: 9
                        }

                        Slot {
                            readonly property int cell: slot - screen.gridFirst
                            visible: Math.floor(cell / 3) < screen.game.craftingSize
                                     && cell % 3 < screen.game.craftingSize
                        }
                    }
                }

                Label {
                    text: "→"
                    color: "white"
                    font.pixelSize: 28
                }

                Repeater {
                    model: SlotRange {
                        source: screen.game.inventory
                        first: screen.resultSlot
                        count: 1
                    }

                    Slot {}
                }
            }

            GridLayout {
                columns: 9
                rowSpacing: 2
                columnSpacing: 2

                // Rows 9..35 (main storage) first, the hotbar 0..8 below with a gap.
                Repeater {
                    model: SlotRange {
                        source: screen.game.inventory
                        count: 36
                    }

                    Slot {
                        Layout.row: slot < 9 ? 4 : Math.floor((slot - 9) / 9)
                        Layout.column: slot % 9
                        Layout.topMargin: slot < 9 ? 12 : 0
                    }
                }
            }

            Label {
                text: qsTr("Left click: pick up and put down. Right click: put down one. E or Esc closes.")
                color: "#e0ffffff"
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            spacing: 6

            Label {
                text: qsTr("Can craft now")
                color: "white"
                font.pixelSize: 18
            }

            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 360
                clip: true
                spacing: 2
                model: screen.game.craftableRecipes

                Label {
                    anchors.centerIn: parent
                    visible: parent.count === 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Collect materials to see recipes here.")
                    color: "#c0ffffff"
                }

                delegate: ItemDelegate {
                    id: recipeRow

                    required property var modelData

                    width: ListView.view.width
                    icon.source: modelData.icon
                    icon.color: "transparent"
                    text: modelData.count > 1 ? qsTr("%1 × %2").arg(modelData.count).arg(modelData.label) : modelData.label
                    onClicked: screen.game.craftRecipe(recipeRow.modelData.recipe)
                }
            }
        }
    }
}
