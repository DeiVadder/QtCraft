pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// The hotbar's radial menu (decisions E-581, E-614): while its pad button (or its key) is held, the
// nine hotbar slots sit on a circle around the crosshair, the right stick (or the mouse) highlights
// one and letting go selects it. The highlighted slot is drawn larger with its name in the middle.
Item {
    id: radial

    required property GameController game
    // The pad's menu or the one of the key with the mouse (decision E-614).
    readonly property int highlighted: game.gamepad.radialOpen ? game.gamepad.radialSlot : game.input.radialSlot
    readonly property real radius: 130

    width: 2 * radius + 80
    height: width

    Rectangle {
        anchors.centerIn: parent
        width: 2 * radial.radius + 76
        height: width
        radius: width / 2
        color: Theme.scrim
        border.color: Theme.panelBorder
    }

    Repeater {
        model: SlotRange {
            source: radial.game.player.inventory
            count: 9
        }

        ItemSlot {
            id: radialSlot

            required property int slot
            required property string icon
            required property int count
            required property real durability
            required property bool empty
            required property string label
            readonly property real angle: 2 * Math.PI * slot / 9
            readonly property bool current: slot === radial.highlighted

            objectName: "radialSlot" + slot
            x: radial.width / 2 + radial.radius * Math.sin(angle) - width / 2
            y: radial.height / 2 - radial.radius * Math.cos(angle) - height / 2
            z: current ? 1 : 0
            scale: current ? 1.3 : 1.0
            // An empty place stays visible against the world.
            baseColor: empty ? Theme.slot : Theme.hudSlot
            activeFocusOnTab: false
            slotIcon: icon
            slotCount: count
            slotDurability: durability
            slotEmpty: empty
            selected: current
            infoEnabled: false

            // The name of the highlighted item, in the middle.
            Label {
                parent: radial
                anchors.centerIn: parent
                width: 2 * radial.radius - 80
                visible: radialSlot.current
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: radialSlot.empty ? qsTr("Empty hand") : radialSlot.label
                color: Theme.text
                style: Text.Outline
                styleColor: Theme.textOutline
                font.pixelSize: 15
                font.bold: true
            }
        }
    }
}
