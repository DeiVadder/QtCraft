pragma ComponentBehavior: Bound

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
    // The game for the items made by loaders (their own `game` property hides the id there).
    readonly property GameController controller: game

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

    // While its pad button is held in the world (decision E-581). The pad's items exist only while
    // the pad leads (the keyboard from its first use on), so without a pad they cost nothing.
    Loader {
        anchors.centerIn: parent
        active: (game.gamepad.active || game.gamepad.radialOpen) && game.phase === GameController.Phase.Playing
        visible: game.phase === GameController.Phase.Playing && game.gamepad.radialOpen

        sourceComponent: HotbarRadial {
            game: root.controller
        }
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

    TradeScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.overlay === "trade"
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
        id: chat

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

    // The pad on the screens (decisions E-583, E-584): its buttons, the ring around its focus and the
    // keyboard for text fields.
    Loader {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        active: game.gamepad.active

        sourceComponent: PadHints {
            game: root.controller
            // The chat line lies along the bottom edge.
            covered: ((keyboard.item as OnScreenKeyboard)?.visible ?? false) || chat.visible
        }
    }

    // The ring's place over the screens (offscreen runs move the screens with it).
    Item {
        id: ringHome

        anchors.fill: parent
        z: 1000

        Loader {
            active: game.gamepad.active

            sourceComponent: FocusRing {
                game: root.controller
                home: ringHome
            }
        }
    }

    // Inside an item of the content, so the keyboard opens in the window that shows the content
    // (offscreen runs move it into another one; a popup of the window itself would stay behind).
    // Made when a field first asks for it.
    Item {
        anchors.fill: parent

        Loader {
            id: keyboard

            active: false

            sourceComponent: OnScreenKeyboard {
                areaWidth: root.width
                areaHeight: root.height
            }
        }
    }

    Connections {
        target: game.gamepad

        function onKeyboardRequested(field) {
            keyboard.active = true;
            (keyboard.item as OnScreenKeyboard).openFor(field);
        }

        // The keyboard or the mouse took over: the field is typed into directly.
        function onActiveChanged() {
            if (!game.gamepad.active && keyboard.item)
                (keyboard.item as OnScreenKeyboard).close();
        }
    }

    // Cmd+W (elsewhere Ctrl+W or Ctrl+F4) closes the window like its close button: the world is
    // saved on the way out. In the world the game leaves Cmd combinations to the system.
    Shortcut {
        sequences: [StandardKey.Close]
        onActivated: root.close()
    }
}
