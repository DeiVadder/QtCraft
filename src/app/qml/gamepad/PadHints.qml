import QtQuick
import QtQuick.Controls
import QtCraft

// The pad's buttons on the screens (decisions E-583, E-612), at the bottom edge while the pad leads
// and a screen has its focus: slot screens pick up and put down, the others select and go back; a
// screen with categories in its side bar (SidebarScreen) adds the shoulders, and one whose page can
// go back to its defaults the Y, asking for a second Y once the first came (docs/UI-Leitfaden.md
// 10, decision E-925). The buttons are drawn as the pad in use
// labels them.
Rectangle {
    id: hints

    required property GameController game
    // Something covers the bottom edge (the on-screen keyboard has hints of its own).
    property bool covered
    readonly property bool slotScreen: [GameController.Overlay.Inventory, GameController.Overlay.Creative, GameController.Overlay.Furnace,
                                        GameController.Overlay.Chest, GameController.Overlay.Trade,
                                        GameController.Overlay.Stonecutter, GameController.Overlay.Enchanting,
                                        GameController.Overlay.Anvil].includes(game.overlay)
                                       && game.phase === GameController.Phase.Playing && !game.handbook.isOpen
    // The focus is in an open popup (a prompt, a list): the shoulders switch no tabs there.
    readonly property bool inPopup: game.gamepad.focusLayer !== null
    // The screen with a side bar of categories around the focus, null for none.
    readonly property SidebarScreen sidebarScreen: {
        for (let item = Window.activeFocusItem; item; item = item.parent) {
            if (item instanceof SidebarScreen)
                return item as SidebarScreen;
        }
        return null;
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Ui.px(4)
    width: row.implicitWidth + Ui.px(24)
    height: row.implicitHeight + Ui.px(10)
    visible: game.gamepad.focusShown && !covered
    radius: Ui.px(4)
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
        ] : hints.inPopup ? [
            { input: "south", text: qsTr("select") },
            { input: "east", text: qsTr("back") }
        ] : [
            { input: "south", text: qsTr("select") },
            { input: "east", text: qsTr("back") }
        ].concat(hints.sidebarScreen ? [{ inputs: ["leftShoulder", "rightShoulder"], text: qsTr("category") }] : [],
                 !hints.sidebarScreen?.resettable ? []
                 : hints.sidebarScreen.resetArmed ? [{ input: "north", text: qsTr("again: reset to default"), name: "padHintResetAgain" }]
                 : [{ input: "north", text: qsTr("reset to default"), name: "padHintReset" }],
                 [{ input: "rightStickMove", text: qsTr("scroll") }])
    }
}
