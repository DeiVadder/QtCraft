import QtQuick
import QtCraft

// The blocky edge of the game's controls (decision E-845): a light line above and on the left and
// a deeper dark line below and on the right, so a button stands out from the panel by its edge, not
// by its surface alone. Sunken swaps them (a pressed button, a field, a chosen segment's
// neighbours). Fills its parent.
Item {
    id: bevel

    property bool sunken: false
    readonly property color upper: sunken ? Theme.bevelDark : Theme.bevelLight
    readonly property color lower: sunken ? Theme.bevelLight : Theme.bevelDark
    readonly property real upperWidth: sunken ? Ui.size.bevelDeep : Ui.size.bevel
    readonly property real lowerWidth: sunken ? Ui.size.bevel : Ui.size.bevelDeep

    anchors.fill: parent

    Rectangle {
        width: parent.width
        height: bevel.upperWidth
        color: bevel.upper
    }
    Rectangle {
        y: bevel.upperWidth
        width: bevel.upperWidth
        height: parent.height - bevel.upperWidth
        color: bevel.upper
    }
    Rectangle {
        y: parent.height - bevel.lowerWidth
        width: parent.width
        height: bevel.lowerWidth
        color: bevel.lower
    }
    Rectangle {
        x: parent.width - bevel.lowerWidth
        width: bevel.lowerWidth
        height: parent.height - bevel.lowerWidth
        color: bevel.lower
    }
}
