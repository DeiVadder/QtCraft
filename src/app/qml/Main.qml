import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    id: root

    width: 1280
    height: 720
    // Shown by main() once the screen is chosen.
    visible: false
    title: qsTr("QtCraft")
    color: Theme.menuBottom

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
        visible: game.phase === "playing" && !game.player.dead
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

    ChestScreen {
        anchors.fill: parent
        visible: game.phase === "playing" && game.overlay === "chest"
        game: game
    }

    PauseMenu {
        anchors.fill: parent
        visible: game.phase === "playing"
                 && (game.overlay === "pause" || game.overlay === "settings" || game.overlay === "controls" || game.overlay === "character")
        game: game
    }

    ChatOverlay {
        anchors.fill: parent
        visible: game.phase === "playing" && game.overlay === "chat"
        game: game
    }

    DeathScreen {
        anchors.fill: parent
        visible: game.phase === "playing" && game.player.dead
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

    // Over everything else, so it opens from the menu, the world and every in-game screen.
    HandbookScreen {
        anchors.fill: parent
        game: game
    }

    // Cmd+W (elsewhere Ctrl+W or Ctrl+F4) closes the window like its close button: the world is
    // saved on the way out. In the world the game leaves Cmd combinations to the system.
    Shortcut {
        sequences: [StandardKey.Close]
        onActivated: root.close()
    }
}
