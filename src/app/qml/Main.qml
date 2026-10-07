import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

ApplicationWindow {
    id: root

    width: 1280
    height: 720
    visible: true
    title: qsTr("QtCraft")
    color: "#87bee9"

    GameController {
        id: game

        objectName: "game"
        view: worldView
        inputEnabled: game.phase === "playing"
    }

    WorldView {
        id: worldView

        anchors.fill: parent
        visible: game.phase === "playing" || game.phase === "loading"
    }

    Hud {
        anchors.fill: parent
        visible: game.phase === "playing"
        debugText: game.debugText
        mouseCaptured: game.mouseCaptured
    }

    MainMenu {
        anchors.fill: parent
        visible: game.phase === "menu"
        onPlayRequested: (worldName, seed) => game.startSingleplayer(worldName, seed)
        onQuitRequested: Qt.quit()
    }

    MessageScreen {
        anchors.fill: parent
        visible: game.phase === "loading" || game.phase === "error"
        text: game.statusText
        showBackButton: game.phase === "error"
        onBackRequested: game.leaveGame()
    }
}
