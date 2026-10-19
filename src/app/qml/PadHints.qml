import QtQuick
import QtQuick.Controls
import QtCraft

// The pad's buttons on the screens (decisions E-583, E-612), at the bottom edge while the pad leads
// and a screen has its focus: slot screens pick up and put down, the others select and go back. The
// buttons are drawn as the pad in use labels them.
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
    width: row.implicitWidth + 24
    height: row.implicitHeight + 10
    visible: game.gamepad.focusShown && !covered
    radius: 4
    color: Theme.hudBackdrop

    PadHintRow {
        id: row

        anchors.centerIn: parent
        game: hints.game
        entries: hints.slotScreen ? [
            { input: "south", text: qsTr("take/put down") },
            { input: "west", text: qsTr("one item") },
            { input: "north", text: qsTr("quick move") },
            { input: "east", text: qsTr("close") },
            { input: "rightStickMove", text: qsTr("scroll") }
        ] : [
            { input: "south", text: qsTr("select") },
            { input: "east", text: qsTr("back") },
            { input: "leftShoulder", text: "" },
            { input: "rightShoulder", text: qsTr("tabs") },
            { input: "rightStickMove", text: qsTr("scroll") }
        ]
    }
}
