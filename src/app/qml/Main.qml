import QtQuick
import QtQuick.Controls
import QtCraft

ApplicationWindow {
    id: root

    width: 1280
    height: 720
    // Every screen fits from this size on (the inventory, the character screen; decision E-231).
    minimumWidth: 1024
    minimumHeight: 640
    // Shown by main() once the screen is chosen.
    visible: false
    title: qsTr("QtCraft")
    color: Theme.menuBottom

    GameController {
        id: game

        objectName: "game"
        view: worldView
        inputEnabled: game.phase === GameController.Phase.Playing
    }

    WorldView {
        id: worldView

        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing || game.phase === GameController.Phase.Loading
    }

    Hud {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && !game.player.dead
        game: game
    }

    InventoryScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.overlay === "inventory"
        game: game
    }

    CreativeScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.overlay === "creative"
        game: game
    }

    FurnaceScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.overlay === "furnace"
        game: game
    }

    ChestScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.overlay === "chest"
        game: game
    }

    PauseMenu {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing
                 && ["pause", "settings", "controls", "debug", "texturepacks", "character", "lan"].includes(game.overlay)
        game: game
    }

    // While the player list key is held; over the world and the HUD, under the screens.
    PlayerListOverlay {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.overlay === "" && game.input.playerListHeld
        game: game
    }

    ChatOverlay {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.overlay === "chat"
        game: game
    }

    DeathScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.player.dead
        game: game
    }

    MainMenu {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Menu
        game: game
        onQuitRequested: Qt.quit()
    }

    MessageScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Loading || game.phase === GameController.Phase.Error
        text: game.statusText
        // Loading a network server's world can be cancelled.
        showBackButton: game.phase === GameController.Phase.Error || game.multiplayer.networkGame
        backText: game.phase === GameController.Phase.Error ? qsTr("Back to menu") : qsTr("Cancel")
        onBackRequested: game.leaveGame()
    }

    DisconnectScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Disconnected
        game: game
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
