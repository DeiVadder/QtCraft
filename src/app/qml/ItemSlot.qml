import QtQuick
import QtQuick.Controls

// One inventory slot: item icon, stack count and a durability bar for tools.
Rectangle {
    id: slot

    property string slotIcon
    property int slotCount
    property real slotDurability: -1
    property bool slotEmpty: true
    property bool selected
    property bool highlighted

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
}
