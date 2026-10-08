import QtQuick
import QtQuick.Controls
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
        visible: game.phase === "playing" && !game.dead
        game: game
    }

    InventoryScreen {
        anchors.fill: parent
        visible: game.phase === "playing" && game.overlay === "inventory"
        game: game
    }

    FurnaceScreen {
        anchors.fill: parent
        visible: game.phase === "playing" && game.overlay === "furnace"
        game: game
    }

    PauseMenu {
        anchors.fill: parent
        visible: game.phase === "playing" && (game.overlay === "pause" || game.overlay === "settings" || game.overlay === "character")
        game: game
    }

    ChatOverlay {
        anchors.fill: parent
        visible: game.phase === "playing" && game.overlay === "chat"
        game: game
    }

    DeathScreen {
        anchors.fill: parent
        visible: game.phase === "playing" && game.dead
        game: game
    }

    MainMenu {
        anchors.fill: parent
        visible: game.phase === "menu"
        game: game
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
