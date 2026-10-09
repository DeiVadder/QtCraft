pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Screen of an open machine (furnace): input, fuel and output above the inventory. Clicks work as
// in the inventory screen; the output can only be taken.
Rectangle {
    id: screen

    required property GameController game
    property int pickedSlot: -1
    // Item under the mouse; the handbook key opens its page.
    property string hoveredItem

    readonly property int inputRow: 46
    readonly property int fuelRow: 47
    readonly property int outputRow: 48

    color: "#90000000"
    focus: visible
    onVisibleChanged: {
        pickedSlot = -1;
        if (visible)
            forceActiveFocus();
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape || screen.game.settings.keys.matches("inventory", event.key)) {
            screen.game.overlay = "";
        } else if (screen.game.settings.keys.matches("handbook", event.key)) {
            screen.game.handbook.open(screen.hoveredItem);
        }
        // Other keys end here as well; Tab moves the focus.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    function clicked(slot, empty, button) {
        if (pickedSlot < 0) {
            if (!empty)
                pickedSlot = slot;
            return;
        }
        if (pickedSlot !== slot && slot !== outputRow)
            game.player.moveItem(pickedSlot, slot, button === Qt.RightButton ? 1 : 0);
        if (button !== Qt.RightButton || slot === outputRow)
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
        required property string itemName

        infoItem: itemName
        infoEnabled: screen.pickedSlot < 0
        onInfoRequested: item => screen.game.handbook.open(item)
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

    ColumnLayout {
        id: content

        anchors.centerIn: parent
        spacing: 12

        Label {
            text: screen.game.player.machineLabel.length > 0 ? screen.game.player.machineLabel : qsTr("Furnace")
            color: "white"
            font.pixelSize: 24
            font.bold: true
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 24

            ColumnLayout {
                spacing: 6

                Repeater {
                    model: SlotRange {
                        source: screen.game.player.inventory
                        first: screen.inputRow
                        count: 1
                    }

                    Slot {
                        Layout.alignment: Qt.AlignHCenter
                    }
                }

                // The flame shrinks as the current fuel burns down.
                Rectangle {
                    objectName: "flame"
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 26
                    implicitHeight: 26
                    radius: 4
                    color: "#60000000"

                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 6
                        height: (parent.height - 4) * screen.game.player.machineBurn
                        radius: 3
                        gradient: Gradient {
                            GradientStop {
                                position: 0
                                color: "#ffe060"
                            }
                            GradientStop {
                                position: 1
                                color: "#e05010"
                            }
                        }
                    }
                }

                Repeater {
                    model: SlotRange {
                        source: screen.game.player.inventory
                        first: screen.fuelRow
                        count: 1
                    }

                    Slot {
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // The arrow fills while the current item is worked on.
            Item {
                objectName: "progressArrow"
                implicitWidth: 72
                implicitHeight: 28

                Rectangle {
                    id: shaft

                    anchors.verticalCenter: parent.verticalCenter
                    width: 52
                    height: 10
                    color: "#60000000"

                    Rectangle {
                        width: parent.width * screen.game.player.machineProgress
                        height: parent.height
                        color: "white"
                    }
                }

                Label {
                    anchors.left: shaft.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▶"
                    color: screen.game.player.machineProgress > 0 ? "white" : "#80ffffff"
                    font.pixelSize: 20
                }
            }

            Repeater {
                model: SlotRange {
                    source: screen.game.player.inventory
                    first: screen.outputRow
                    count: 1
                }

                Slot {
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
                    source: screen.game.player.inventory
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
            text: qsTr("Put something to smelt on top and fuel below. Left click: pick up and put down. Right click: put down one. %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
            color: "#e0ffffff"
        }
    }
}
