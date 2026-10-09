import QtQuick
import QtQuick.Controls

// One inventory slot: item icon, stack count and a durability bar for tools. While highlighted
// (hovered), a filled slot shows a small "?" that asks for the item's handbook page.
Rectangle {
    id: slot

    // Item name for the handbook; empty hides the "?". Screens switch it off while a stack is
    // picked up, so a click anywhere on the slot still puts it down.
    property string infoItem
    property bool infoEnabled: true
    property string slotIcon
    property int slotCount
    property real slotDurability: -1
    property bool slotEmpty: true
    property bool selected
    property bool highlighted

    signal infoRequested(string item)

    implicitWidth: 52
    implicitHeight: 52
    color: highlighted ? "#c08a8a8a" : "#a0303030"
    border.color: selected ? "white" : "#80000000"
    border.width: selected ? 3 : 1
    radius: 2

    Image {
        anchors.fill: parent
        anchors.margins: 8
        visible: !slot.slotEmpty
        source: slot.slotIcon
        smooth: false
        sourceSize: Qt.size(32, 32)
    }

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 3
        visible: !slot.slotEmpty && slot.slotCount > 1
        text: slot.slotCount
        color: "white"
        style: Text.Outline
        styleColor: "black"
        font.pixelSize: 13
        font.bold: true
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 5
        visible: !slot.slotEmpty && slot.slotDurability >= 0 && slot.slotDurability < 1
        height: 3
        width: (parent.width - 10) * slot.slotDurability
        color: slot.slotDurability > 0.5 ? "#40d040" : (slot.slotDurability > 0.2 ? "#e0c030" : "#e04030")
    }

    // Its own MouseArea takes the click, so the slot's pick-up and put-down handling never sees it.
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 2
        width: 16
        height: 16
        radius: 8
        visible: slot.infoEnabled && slot.highlighted && !slot.slotEmpty && slot.infoItem.length > 0
        color: infoArea.containsMouse ? "#f0ffd27a" : "#c0202020"
        border.color: "#c0ffffff"

        Label {
            anchors.centerIn: parent
            text: "?"
            color: infoArea.containsMouse ? "black" : "white"
            font.pixelSize: 11
            font.bold: true
        }

        MouseArea {
            id: infoArea

            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: slot.infoRequested(slot.infoItem)
        }
    }
}
