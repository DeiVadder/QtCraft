import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Rectangle {
    id: menu

    required property GameController game

    color: Theme.scrim
    focus: visible
    onVisibleChanged: {
        if (visible) {
            forceActiveFocus();
            // Made by its LazyScreen when the overlay changed to one of its sub-screens: opens it.
            followOverlay();
        } else {
            // Its sub-screens belong to the menu: they close (and store the settings) with it.
            settingsScreen.close();
            characterScreen.close();
            lanScreen.close();
        }
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            menu.game.overlay = GameController.Overlay.None;
        } else if (menu.game.settings.keys.matches("handbook", event.key)) {
            menu.game.handbook.open("");
        }
        // Other keys end here as well; Tab moves between the buttons.
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 320)
        spacing: 10

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Game paused")
            color: Theme.text
            font.pixelSize: 28
            font.bold: true
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Back to game")
            highlighted: true
            onClicked: menu.game.overlay = GameController.Overlay.None
        }

        Button {
            Layout.fillWidth: true
            visible: menu.game.persistent
            text: qsTr("Save world")
            onClicked: {
                menu.game.saveWorld();
                menu.game.overlay = GameController.Overlay.None;
            }
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Settings")
            onClicked: menu.game.overlay = GameController.Overlay.Settings
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Character")
            onClicked: menu.game.overlay = GameController.Overlay.Character
        }

        Button {
            Layout.fillWidth: true
            visible: !menu.game.multiplayer.networkGame
            text: menu.game.multiplayer.lanPort > 0 ? qsTr("Open on port %1").arg(menu.game.multiplayer.lanPort) : qsTr("Open to LAN")
            onClicked: menu.game.overlay = GameController.Overlay.Lan
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Handbook (%1)").arg(menu.game.settings.keys.labels.handbook)
            onClicked: menu.game.handbook.open("")
        }

        Button {
            Layout.fillWidth: true
            text: menu.game.multiplayer.networkGame ? qsTr("Disconnect")
                  : menu.game.persistent ? qsTr("Save and quit to menu") : qsTr("Quit to menu")
            onClicked: menu.game.leaveGame()
        }
    }

    // The settings pages in the order of SettingsScreen.startPage.
    readonly property list<int> settingsPages: [GameController.Overlay.Settings, GameController.Overlay.Controls,
        GameController.Overlay.Gamepad, GameController.Overlay.Debug, GameController.Overlay.TexturePacks]

    // The settings are an overlay of their own (Overlay.Settings) so the --overlay test hook can
    // open them. Closing a sub-screen returns to the menu and its keys.
    function subScreenClosed(overlays: list<int>) {
        // The debug options open from the world (debug options key) and return there.
        if (menu.game.overlay === GameController.Overlay.Debug)
            menu.game.overlay = GameController.Overlay.None;
        else if (overlays.includes(menu.game.overlay))
            menu.game.overlay = GameController.Overlay.Pause;
        if (visible)
            forceActiveFocus();
    }

    SettingsScreen {
        id: settingsScreen

        game: menu.game
        onClosed: menu.subScreenClosed(menu.settingsPages)
    }

    CharacterScreen {
        id: characterScreen

        game: menu.game
        onClosed: menu.subScreenClosed([GameController.Overlay.Character])
    }

    LanScreen {
        id: lanScreen

        game: menu.game
        onClosed: menu.subScreenClosed([GameController.Overlay.Lan])
    }

    // One sub-screen at a time, the one the overlay names; any other overlay closes them.
    function followOverlay() {
        const overlay = menu.game.overlay;
        if (!menu.settingsPages.includes(overlay))
            settingsScreen.close();
        if (overlay !== GameController.Overlay.Character)
            characterScreen.close();
        if (overlay !== GameController.Overlay.Lan)
            lanScreen.close();
        if (menu.settingsPages.includes(overlay)) {
            settingsScreen.startPage = menu.settingsPages.indexOf(overlay);
            settingsScreen.open();
        } else if (overlay === GameController.Overlay.Character) {
            characterScreen.open();
        } else if (overlay === GameController.Overlay.Lan) {
            lanScreen.open();
        }
    }

    Connections {
        target: menu.game

        function onOverlayChanged() {
            menu.followOverlay();
        }
    }
}
