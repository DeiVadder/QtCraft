pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Screen of an open storage block (chest): its slots above the inventory. Clicks work as in the
// inventory screen.
Rectangle {
    id: screen

    required property GameController game
    property int pickedSlot: -1

    readonly property int firstStorageRow: 46

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
        }
    }

    function clicked(slot, empty, button) {
        if (pickedSlot < 0) {
            if (!empty)
                pickedSlot = slot;
            return;
        }
        if (pickedSlot !== slot)
            game.moveItem(pickedSlot, slot, button === Qt.RightButton ? 1 : 0);
        if (button !== Qt.RightButton)
            pickedSlot = -1;
    }

    MouseArea {
        anchors.fill: parent
        onClicked: screen.pickedSlot = -1
    }

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
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onTapped: (eventPoint, button) => screen.clicked(slotItem.slot, slotItem.empty, button)
        }
    }

    ColumnLayout {
        id: content

        anchors.centerIn: parent
        spacing: 12

        Label {
            text: screen.game.machineLabel.length > 0 ? screen.game.machineLabel : qsTr("Chest")
            color: "white"
            font.pixelSize: 24
            font.bold: true
        }

        GridLayout {
            objectName: "storage"
            columns: 9
            rowSpacing: 2
            columnSpacing: 2

            Repeater {
                model: SlotRange {
                    source: screen.game.inventory
                    first: screen.firstStorageRow
                    count: screen.game.containerSlots
                }

                Slot {
                    readonly property int storageIndex: slot - screen.firstStorageRow

                    Layout.row: Math.floor(storageIndex / 9)
                    Layout.column: storageIndex % 9
                }
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
            Layout.maximumWidth: 9 * 54
            wrapMode: Text.Wrap
            text: qsTr("Left click: pick up and put down. Right click: put down one. E or Esc closes. Breaking the chest drops its contents.")
            color: "#e0ffffff"
        }
    }
}
