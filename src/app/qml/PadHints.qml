import QtQuick
import QtQuick.Controls
import QtCraft

// The pad's buttons on the screens (decision E-583), at the bottom edge while the pad leads and a
// screen has its focus: slot screens pick up and put down, the others select and go back.
Rectangle {
    id: hints

    required property GameController game
    // Something covers the bottom edge (the on-screen keyboard has hints of its own).
    property bool covered
    readonly property bool slotScreen: ["inventory", "creative", "furnace", "chest", "trade"].includes(game.overlay)
                                       && game.phase === GameController.Phase.Playing && !game.handbook.isOpen

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 4
    width: label.implicitWidth + 24
    height: label.implicitHeight + 10
    visible: game.gamepad.focusShown && !covered
    radius: 4
    color: Theme.hudBackdrop

    Label {
        id: label

        anchors.centerIn: parent
        text: hints.slotScreen ? qsTr("A: take/put down  X: one item  Y: quick move  B: close  Right stick: scroll")
                               : qsTr("A: select  B: back  LB/RB: tabs  Right stick: scroll")
        color: Theme.text
        font.pixelSize: 13
    }
}
