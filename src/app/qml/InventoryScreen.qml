pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Inventory: click a slot to pick its stack up, click another slot to put it there (merging or swapping).
Rectangle {
    id: screen

    required property GameController game
    property int pickedSlot: -1

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

    MouseArea {
        anchors.fill: parent
        onClicked: screen.pickedSlot = -1
    }

    // Opaque panel so the crosshair and the world do not show through the slots.
    Rectangle {
        anchors.fill: inventoryContent
        anchors.margins: -20
        radius: 6
        color: "#e8282828"
        border.color: "#80ffffff"
    }

    ColumnLayout {
        id: inventoryContent

        anchors.centerIn: parent
        spacing: 12

        Label {
            text: qsTr("Inventory")
            color: "white"
            font.pixelSize: 24
            font.bold: true
        }

        GridLayout {
            id: grid

            columns: 9
            rowSpacing: 2
            columnSpacing: 2

            // Rows 9..35 (main storage) first, the hotbar 0..8 below with a gap.
            Repeater {
                model: screen.game.inventory

                ItemSlot {
                    id: inventorySlot

                    required property int index
                    required property string icon
                    required property int count
                    required property real durability
                    required property bool empty
                    required property string label

                    Layout.row: index < 9 ? 4 : Math.floor((index - 9) / 9)
                    Layout.column: index % 9
                    Layout.topMargin: index < 9 ? 12 : 0
                    slotIcon: icon
                    slotCount: count
                    slotDurability: durability
                    slotEmpty: empty
                    selected: index === screen.pickedSlot
                    highlighted: hover.hovered

                    ToolTip.visible: hover.hovered && !empty
                    ToolTip.text: label

                    HoverHandler {
                        id: hover
                    }

                    TapHandler {
                        onTapped: {
                            if (screen.pickedSlot < 0) {
                                if (!inventorySlot.empty)
                                    screen.pickedSlot = inventorySlot.index;
                            } else {
                                screen.game.moveItem(screen.pickedSlot, inventorySlot.index);
                                screen.pickedSlot = -1;
                            }
                        }
                    }
                }
            }
        }

        Label {
            text: qsTr("Click a stack, then click where it should go. E or Esc closes.")
            color: "#e0ffffff"
        }
    }
}
