import QtQuick
import QtQuick.Controls
import QtCraft

// One inventory slot: item icon, stack count and a durability bar for tools. While highlighted
// (hovered), a filled slot shows a small "?" that asks for the item's handbook page. A slot whose
// stack is picked up is dimmed; the stack itself is drawn at the mouse (`carried`).
Rectangle {
    id: slot

    // Item name for the handbook; empty hides the "?". Screens switch it off while a stack is
    // picked up, so a click anywhere on the slot still puts it down.
    property string infoItem
    property bool infoEnabled: true
    property string slotIcon
    // Shown faintly while the slot is empty (what belongs into an armour slot).
    property string placeholderIcon
    property int slotCount
    property real slotDurability: -1
    property bool slotEmpty: true
    property bool selected
    property bool highlighted
    property bool picked
    property bool carried
    // Background of an idle slot (the hotbar lies on the world and is darker).
    property color baseColor: Theme.slot

    signal infoRequested(string item)

    implicitWidth: 52
    implicitHeight: 52
    // The pad's focus reaches slots that take clicks (decision E-583); a carried stack is a picture.
    activeFocusOnTab: enabled && !carried
    color: carried ? Theme.clear : (picked ? Theme.slotPicked : (highlighted ? Theme.slotHover : baseColor))
    border.color: carried ? Theme.clear : (selected ? Theme.selection : Theme.slotBorder)
    border.width: selected ? 3 : 1
    radius: 2

    Rectangle {
        anchors.fill: parent
        anchors.margins: 6
        visible: !slot.slotEmpty && !slot.carried && !slot.picked
        radius: 6
        color: Theme.iconBackdrop
    }

    Image {
        anchors.fill: parent
        anchors.margins: 10
        visible: slot.slotEmpty && slot.placeholderIcon.length > 0
        opacity: 0.25
        source: slot.placeholderIcon
        smooth: false
        sourceSize: Qt.size(32, 32)
    }

    Image {
        anchors.fill: parent
        anchors.margins: slot.carried ? 4 : 8
        visible: !slot.slotEmpty
        opacity: slot.picked ? 0.3 : 1.0
        source: slot.slotIcon
        smooth: false
        sourceSize: Qt.size(32, 32)
    }

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 3
        visible: !slot.slotEmpty && slot.slotCount > 1 && !slot.picked
        text: slot.slotCount
        color: Theme.text
        style: Text.Outline
        styleColor: Theme.textOutline
        font.pixelSize: 13
        font.bold: true
    }

    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 5
        visible: !slot.slotEmpty && !slot.picked && slot.slotDurability >= 0 && slot.slotDurability < 1
        height: 3
        width: (parent.width - 10) * slot.slotDurability
        color: slot.slotDurability > 0.5 ? Theme.durabilityGood : (slot.slotDurability > 0.2 ? Theme.durabilityLow : Theme.durabilityBad)
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
        color: infoArea.containsMouse ? Theme.infoButtonHover : Theme.infoButton
        border.color: Theme.panelBorder

        Label {
            anchors.centerIn: parent
            text: "?"
            color: infoArea.containsMouse ? Theme.infoButton : Theme.text
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
