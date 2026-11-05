// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// The screens of the window over the world and the HUD, in the order they cover each other: the
// in-game screens, the player list, the chat line, the sign editor and the death screen, then the
// screens of the phases (menu, error, loading, disconnect), the start screen and the handbook over
// everything. Apart from the inventory and the chat line they exist only while shown and for a
// while after (OverlayScreen, decision E-691).
Item {
    id: screens

    required property GameController game
    // The HUD under the screens: the screens over the world keep its status rows clear.
    required property Hud hud
    // The item the handbook gives the focus back to when it closes.
    property Item focusBeforeHandbook: null
    // The chat line is shown (the pad's hints make room for it).
    readonly property bool chatShown: chat.visible
    // The height of the HUD's status rows at the bottom, 0 while the HUD is hidden.
    readonly property real statusReserved: hud.visible ? hud.height - hud.statusArea.y : 0

    InventoryScreen {
        anchors.fill: parent
        visible: screens.game.phase === GameController.Phase.Playing
                 && screens.game.overlay === GameController.Overlay.Inventory
        game: screens.game
    }

    OverlayScreen {
        overlay: GameController.Overlay.Creative
        // About 30 ms to make in Release (E-690): made with the creative world, not on the first E.
        preload: screens.game.phase === GameController.Phase.Playing && screens.game.player.creative

        screen: CreativeScreen { game: screens.game }
    }

    OverlayScreen {
        overlay: GameController.Overlay.Furnace

        screen: FurnaceScreen { game: screens.game }
    }

    OverlayScreen {
        overlay: GameController.Overlay.Chest

        screen: ChestScreen { game: screens.game }
    }

    OverlayScreen {
        overlay: GameController.Overlay.Trade

        screen: TradeScreen { game: screens.game }
    }

    OverlayScreen {
        overlay: GameController.Overlay.Stonecutter

        screen: StonecutterScreen { game: screens.game }
    }

    OverlayScreen {
        overlay: GameController.Overlay.Enchanting

        screen: EnchantingScreen { game: screens.game }
    }

    OverlayScreen {
        overlay: GameController.Overlay.Anvil

        screen: AnvilScreen { game: screens.game }
    }

    // A moment after the world starts to play, the pause menu is made in the background (decision
    // E-875): after the world's first frames and the creative screen, before the first Esc.
    Timer {
        id: pauseMenuDue

        // The world has played for pauseMenuDue.interval; false again once it stops playing.
        property bool reached: false
        readonly property bool playing: screens.game.phase === GameController.Phase.Playing

        interval: 500
        running: playing && !reached
        onTriggered: reached = true
        onPlayingChanged: {
            if (!playing)
                reached = false;
        }
    }

    // With its sub-screens: settings, character, open to LAN and manage server (each made when it
    // first opens). Made in the background (asynchronously, hidden) a moment after the world starts
    // to play and kept while it plays, so Esc opens it at once (decision E-875); without a
    // keep-alive time, so it is given back as soon as the world stops playing.
    OverlayScreen {
        open: screens.game.pauseMenuOpen
        preload: pauseMenuDue.reached
        keepAliveMs: 0

        screen: PauseMenu {
            game: screens.game
            statusReserved: screens.statusReserved
        }
    }

    // The tree of the adventure guide (decision E-924).
    OverlayScreen {
        overlay: GameController.Overlay.Advancements

        screen: AdvancementsScreen {
            game: screens.game
            onCloseRequested: screens.game.closeAdvancements()
        }
    }

    // While the player list key is held; over the world and the HUD, under the screens.
    OverlayScreen {
        open: screens.game.overlay === GameController.Overlay.None && screens.game.input.playerListHeld
              && !screens.game.photo.hudHidden

        screen: PlayerListOverlay { game: screens.game }
    }

    ChatOverlay {
        id: chat

        anchors.fill: parent
        // Same frame as the HUD (both fill the window): the /help hint stays clear of the hotbar.
        statusArea: screens.hud.visible ? screens.hud.statusArea : Qt.rect(width, height, 0, 0)
        visible: screens.game.phase === GameController.Phase.Playing
                 && screens.game.overlay === GameController.Overlay.Chat
        game: screens.game
    }

    OverlayScreen {
        overlay: GameController.Overlay.Sign

        screen: SignEditor { game: screens.game }
    }

    OverlayScreen {
        open: screens.game.player.dead

        screen: DeathScreen {
            game: screens.game
            statusReserved: screens.statusReserved
        }
    }

    // Only in the menu phase: in the world its objects are given back once the keep-alive time ran out.
    OverlayScreen {
        phase: GameController.Phase.Menu
        // Made at once (decision E-875): it is the first screen at start-up, where test hooks and
        // the pad act on it right away, and when a world closes nothing plays that a stall could cut.
        asynchronous: false

        screen: MainMenu {
            game: screens.game
            onQuitRequested: Qt.quit()
        }
    }

    OverlayScreen {
        phase: GameController.Phase.Error

        screen: MessageScreen {
            keys: screens.game.settings.keys
            text: screens.game.statusText
            showBackButton: true
            onBackRequested: screens.game.leaveGame()
        }
    }

    // Made within the click (decision E-943): it shows in the next frame, before the start blocks
    // the GUI thread.
    OverlayScreen {
        phase: GameController.Phase.Loading
        asynchronous: false

        screen: LoadingScreen {
            progress: screens.game.loading
            keys: screens.game.settings.keys
            // Joining a network server can be cancelled, entering another dimension cannot.
            canCancel: screens.game.loading.cancellable
            onCancelRequested: screens.game.leaveGame()
        }
    }

    OverlayScreen {
        phase: GameController.Phase.Disconnected

        screen: DisconnectScreen { game: screens.game }
    }

    // The start screen (decision E-943): the window's first frames while the game loads its
    // worlds, mods and content, over the menu made already; unloaded at once, never logged.
    OverlayScreen {
        phase: -1
        open: screens.game.starting
        keepAliveMs: 0
        logCreation: false
        asynchronous: false

        screen: StartScreen {}
    }

    // Over everything else, so it opens from the menu, the world and every in-game screen.
    OverlayScreen {
        phase: -1
        open: screens.game.handbook.isOpen

        screen: HandbookScreen {
            game: screens.game
            focusOutside: screens.focusBeforeHandbook
        }
    }
}
