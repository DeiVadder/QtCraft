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
        } else {
            // Its sub-screens belong to the menu: they close (and store the settings) with it.
            settingsScreen.close();
            characterScreen.close();
            lanScreen.close();
        }
    }
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            menu.game.overlay = "";
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
            onClicked: menu.game.overlay = ""
        }

        Button {
            Layout.fillWidth: true
            visible: menu.game.persistent
            text: qsTr("Save world")
            onClicked: {
                menu.game.saveWorld();
                menu.game.overlay = "";
            }
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Settings")
            onClicked: menu.game.overlay = "settings"
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Character")
            onClicked: menu.game.overlay = "character"
        }

        Button {
            Layout.fillWidth: true
            visible: !menu.game.multiplayer.networkGame
            text: menu.game.multiplayer.lanPort > 0 ? qsTr("Open on port %1").arg(menu.game.multiplayer.lanPort) : qsTr("Open to LAN")
            onClicked: menu.game.overlay = "lan"
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

    // The settings are an overlay of their own ("settings") so the --overlay test hook can open them.
    // Closing a sub-screen returns to the menu and its keys.
    function subScreenClosed(overlays) {
        // The debug options open from the world (debug options key) and return there.
        if (menu.game.overlay === "debug")
            menu.game.overlay = "";
        else if (overlays.includes(menu.game.overlay))
            menu.game.overlay = "pause";
        if (visible)
            forceActiveFocus();
    }

    SettingsScreen {
        id: settingsScreen

        game: menu.game
        onClosed: menu.subScreenClosed(["settings", "controls", "debug"])
    }

    CharacterScreen {
        id: characterScreen

        game: menu.game
        onClosed: menu.subScreenClosed(["character"])
    }

    LanScreen {
        id: lanScreen

        game: menu.game
        onClosed: menu.subScreenClosed(["lan"])
    }

    // One sub-screen at a time, the one the overlay names; any other overlay closes them.
    Connections {
        target: menu.game

        function onOverlayChanged() {
            const overlay = menu.game.overlay;
            const settingsPages = ["settings", "controls", "debug"];
            if (!settingsPages.includes(overlay))
                settingsScreen.close();
            if (overlay !== "character")
                characterScreen.close();
            if (overlay !== "lan")
                lanScreen.close();
            if (settingsPages.includes(overlay)) {
                settingsScreen.startPage = settingsPages.indexOf(overlay);
                settingsScreen.open();
            } else if (overlay === "character") {
                characterScreen.open();
            } else if (overlay === "lan") {
                lanScreen.open();
            }
        }
    }
}
