import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Rectangle {
    id: menu

    required property GameController game

    color: "#a0000000"
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            menu.game.overlay = "";
            event.accepted = true;
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 320)
        spacing: 10

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Game paused")
            color: "white"
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
            text: menu.game.persistent ? qsTr("Save and quit to menu") : qsTr("Quit to menu")
            onClicked: menu.game.leaveGame()
        }
    }

    // The settings are an overlay of their own ("settings") so the --overlay test hook can open them.
    SettingsScreen {
        id: settingsScreen

        game: menu.game
        onClosed: if (menu.game.overlay === "settings") menu.game.overlay = "pause"
    }

    CharacterScreen {
        id: characterScreen

        game: menu.game
        onClosed: if (menu.game.overlay === "character") menu.game.overlay = "pause"
    }

    Connections {
        target: menu.game

        function onOverlayChanged() {
            if (menu.game.overlay === "settings") {
                settingsScreen.open();
            } else if (menu.game.overlay === "character") {
                characterScreen.open();
            }
        }
    }
}
