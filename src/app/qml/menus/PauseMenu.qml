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
            adminScreen.close();
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
        // Hidden while one of its screens is open: it would show around a small one. The handbook
        // covers it as well (and keeps Tab).
        visible: !settingsScreen.visible && !characterScreen.visible && !lanScreen.visible && !adminScreen.visible
                 && !menu.game.handbook.isOpen
        width: Math.min(parent.width - 2 * Ui.space.l, Ui.screen.column)
        spacing: Ui.space.s

        Label {
            Layout.alignment: Qt.AlignHCenter
            // The world runs on while others play in it (decision E-877).
            text: menu.game.pause.hint === WorldPause.None ? qsTr("Game paused") : qsTr("Game menu")
            color: Theme.text
            Layout.bottomMargin: menu.game.pause.hint === WorldPause.None ? Ui.space.s : 0
            font.pixelSize: Ui.type.title
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            Layout.bottomMargin: Ui.space.s
            visible: menu.game.pause.hint !== WorldPause.None
            text: menu.game.pause.hint === WorldPause.OthersConnected
                  ? qsTr("The game keeps running: other players are connected.")
                  : menu.game.pause.hint === WorldPause.Pending
                    ? qsTr("The game pauses as soon as the world around you has loaded.")
                    : qsTr("The game keeps running on the server.")
            color: Theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        GameButton {
            Layout.fillWidth: true
            kind: GameButton.Primary
            large: true
            text: qsTr("Back to game")
            onClicked: menu.game.overlay = GameController.Overlay.None
        }

        GameButton {
            Layout.fillWidth: true
            large: true
            visible: menu.game.persistent
            text: qsTr("Save world")
            onClicked: {
                menu.game.saveWorld();
                menu.game.overlay = GameController.Overlay.None;
            }
        }

        GameButton {
            Layout.fillWidth: true
            large: true
            text: qsTr("Settings")
            onClicked: menu.game.overlay = GameController.Overlay.Settings
        }

        GameButton {
            Layout.fillWidth: true
            large: true
            text: qsTr("Character")
            onClicked: menu.game.overlay = GameController.Overlay.Character
        }

        GameButton {
            Layout.fillWidth: true
            large: true
            visible: !menu.game.multiplayer.networkGame
            text: menu.game.multiplayer.lanPort > 0 ? qsTr("Open on port %1").arg(menu.game.multiplayer.lanPort) : qsTr("Open to LAN")
            onClicked: menu.game.overlay = GameController.Overlay.Lan
        }

        // Only for operators (decision E-863): the server tells each player whether he is one.
        GameButton {
            objectName: "manageServerButton"
            Layout.fillWidth: true
            large: true
            visible: menu.game.admin.operatorRights
            text: qsTr("Manage server")
            onClicked: menu.game.overlay = GameController.Overlay.Admin
        }

        GameButton {
            objectName: "advancementsButton"
            Layout.fillWidth: true
            large: true
            text: qsTr("Advancements")
            keyHint: menu.game.settings.keys.labels.advancements
            onClicked: menu.game.overlay = GameController.Overlay.Advancements
        }

        GameButton {
            Layout.fillWidth: true
            large: true
            text: qsTr("Handbook")
            keyHint: menu.game.settings.keys.labels.handbook
            onClicked: menu.game.handbook.open("")
        }

        GameButton {
            Layout.fillWidth: true
            large: true
            text: menu.game.multiplayer.networkGame ? qsTr("Disconnect")
                  : menu.game.persistent ? qsTr("Save and quit to menu") : qsTr("Quit to menu")
            onClicked: menu.game.leaveGame()
        }
    }

    // The overlays that open the settings (each at its page, SettingsScreen.pageOfOverlay).
    readonly property list<int> settingsPages: [GameController.Overlay.Settings, GameController.Overlay.Display,
        GameController.Overlay.Audio, GameController.Overlay.Interface, GameController.Overlay.Controls,
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

    // One level down from the pause menu, as from the main menu (SidebarScreen); the debug options
    // open from the world (debug options key) and lead back there.
    SettingsScreen {
        id: settingsScreen

        game: menu.game
        inGame: true
        // The handbook opened from the settings covers them (and keeps Tab).
        covered: menu.game.handbook.isOpen
        backText: menu.game.overlay === GameController.Overlay.Debug ? qsTr("‹ Back to game") : qsTr("‹ Pause menu")
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

    AdminScreen {
        id: adminScreen

        game: menu.game
        onClosed: menu.subScreenClosed([GameController.Overlay.Admin])
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
        if (overlay !== GameController.Overlay.Admin)
            adminScreen.close();
        if (menu.settingsPages.includes(overlay)) {
            settingsScreen.startPage = settingsScreen.pageOfOverlay(overlay);
            settingsScreen.open();
        } else if (overlay === GameController.Overlay.Character) {
            characterScreen.open();
        } else if (overlay === GameController.Overlay.Lan) {
            lanScreen.open();
        } else if (overlay === GameController.Overlay.Admin && !adminScreen.visible) {
            adminScreen.open();
        }
    }

    Connections {
        target: menu.game

        function onOverlayChanged() {
            menu.followOverlay();
        }
    }
}
