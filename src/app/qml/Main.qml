// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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
    // The item that had the focus before the handbook opened; the handbook gives the focus back to
    // it. Kept here rather than read when the handbook opens: the screens it covers hide at the same
    // moment (and lose the focus) in no fixed order.
    property Item focusBeforeHandbook: null
    // Where the advancements lead back to: the pause menu when they opened from it, else the world.
    property int advancementsReturn: GameController.Overlay.None
    property int lastOverlay: GameController.Overlay.None

    onActiveFocusItemChanged: {
        if (!gameController.handbook.isOpen && activeFocusItem)
            focusBeforeHandbook = activeFocusItem;
    }

    // The UI scale (decision E-725): the player's choice and the window; Ui.px() and
    // Ui.hudPx() take their factors from here.
    Binding {
        target: UiScaling
        property: "setting"
        value: gameController.settings.uiScale
    }
    Binding {
        target: UiScaling
        property: "windowSize"
        value: Qt.size(root.width, root.height)
    }
    // The chat lines of the HUD all show while the chat line is open and fade while it is closed
    // (decision E-875).
    Binding {
        target: gameController.hud.chat
        property: "open"
        value: gameController.overlay === GameController.Overlay.Chat
    }
    // Controls (buttons, boxes, labels, tooltips, popups) inherit the window's font.
    WindowFont {
        target: root
    }

    GameController {
        id: gameController

        objectName: "game"
        view: worldView
        inputEnabled: gameController.phase === GameController.Phase.Playing
    }

    WorldView {
        id: worldView

        anchors.fill: parent
        visible: gameController.phase === GameController.Phase.Playing
                 || gameController.phase === GameController.Phase.Loading
    }

    // The photo mode's key hides it with the crosshair and every overlay (decision E-692); nothing
    // of it shows behind a full-screen screen (decision E-927), the rest hides by GameController.hudShown
    // (behind the death screen only the status rows stay).
    Hud {
        id: hud

        anchors.fill: parent
        visible: gameController.phase === GameController.Phase.Playing && !gameController.photo.hudHidden
                 && gameController.hudShown !== GameController.HudShown.Nothing
        game: gameController
    }

    // While its pad button or its key is held in the world, not with the HUD hidden (decisions E-581,
    // E-614, E-692). The pad's items exist only while the pad leads (the keyboard from its first use
    // on), so without a pad they cost nothing.
    Loader {
        anchors.centerIn: parent
        active: (gameController.gamepad.active || gameController.gamepad.radialOpen || gameController.input.radialOpen)
                && gameController.phase === GameController.Phase.Playing
        visible: gameController.phase === GameController.Phase.Playing
                 && (gameController.gamepad.radialOpen || gameController.input.radialOpen)
                 && !gameController.photo.hudHidden

        sourceComponent: HotbarRadial {
            game: gameController
        }
    }

    InventoryScreen {
        anchors.fill: parent
        visible: gameController.phase === GameController.Phase.Playing
                 && gameController.overlay === GameController.Overlay.Inventory
        game: gameController
    }

    // The other screens exist only while shown and for a while after (LazyScreen, decision E-691).
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Creative
        // About 30 ms to make in Release (E-690): made with the creative world, not on the first E.
        preload: gameController.phase === GameController.Phase.Playing && gameController.player.creative

        screen: CreativeScreen {
            game: gameController
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Furnace

        screen: FurnaceScreen {
            game: gameController
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Chest

        screen: ChestScreen {
            game: gameController
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Trade

        screen: TradeScreen {
            game: gameController
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Stonecutter

        screen: StonecutterScreen {
            game: gameController
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Enchanting

        screen: EnchantingScreen {
            game: gameController
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Anvil

        screen: AnvilScreen {
            game: gameController
        }
    }

    // With its sub-screens: settings, character and open to LAN.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && [GameController.Overlay.Pause, GameController.Overlay.Settings, GameController.Overlay.Display, GameController.Overlay.Audio,
                   GameController.Overlay.Interface, GameController.Overlay.Controls,
                   GameController.Overlay.Gamepad, GameController.Overlay.Debug, GameController.Overlay.TexturePacks,
                   GameController.Overlay.Character, GameController.Overlay.Lan, GameController.Overlay.Admin].includes(gameController.overlay)

        screen: PauseMenu {
            game: gameController
            statusReserved: hud.visible ? hud.height - hud.statusArea.y : 0
        }
    }

    // The tree of the adventure guide (decision E-924).
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Advancements

        screen: AdvancementsScreen {
            game: gameController
            onCloseRequested: gameController.overlay = root.advancementsReturn
        }
    }

    Connections {
        target: gameController

        function onOverlayChanged() {
            if (gameController.overlay === GameController.Overlay.Advancements)
                root.advancementsReturn = root.lastOverlay === GameController.Overlay.Pause ? GameController.Overlay.Pause : GameController.Overlay.None;
            root.lastOverlay = gameController.overlay;
        }
    }

    // While the player list key is held; over the world and the HUD, under the screens.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.None && gameController.input.playerListHeld
               && !gameController.photo.hudHidden

        screen: PlayerListOverlay {
            game: gameController
        }
    }

    ChatOverlay {
        id: chat

        anchors.fill: parent
        // Same frame as the HUD (both fill the window): the /help hint stays clear of the hotbar.
        statusArea: hud.visible ? hud.statusArea : Qt.rect(width, height, 0, 0)
        visible: gameController.phase === GameController.Phase.Playing
                 && gameController.overlay === GameController.Overlay.Chat
        game: gameController
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing
               && gameController.overlay === GameController.Overlay.Sign

        screen: SignEditor {
            game: gameController
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Playing && gameController.player.dead

        screen: DeathScreen {
            game: gameController
            statusReserved: hud.visible ? hud.height - hud.statusArea.y : 0
        }
    }

    // Only in the menu phase: in the world its objects are given back once the keep-alive time ran out.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Menu
        // Made at once (decision E-875): it is the first screen at start-up, where test hooks and
        // the pad act on it right away, and when a world closes nothing plays that a stall could cut.
        asynchronous: false

        screen: MainMenu {
            game: gameController
            onQuitRequested: Qt.quit()
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Error

        screen: MessageScreen {
            text: gameController.statusText
            showBackButton: true
            onBackRequested: gameController.leaveGame()
        }
    }

    // Made within the click (decision E-943): it shows in the next frame, before the start blocks
    // the GUI thread.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Loading
        asynchronous: false

        screen: LoadingScreen {
            progress: gameController.loading
            // Loading a network server's world can be cancelled.
            canCancel: gameController.multiplayer.networkGame
            onCancelRequested: gameController.leaveGame()
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.phase === GameController.Phase.Disconnected

        screen: DisconnectScreen {
            game: gameController
        }
    }

    // The start screen (decision E-943): the window's first frames while the game loads its
    // worlds, mods and content, over the menu made already.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: 0
        shown: gameController.starting
        asynchronous: false

        screen: StartScreen {}
    }

    // Over everything else, so it opens from the menu, the world and every in-game screen.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: gameController.screenKeepAliveMs
        logCreation: gameController.logScreenCreation
        shown: gameController.handbook.isOpen

        screen: HandbookScreen {
            game: gameController
            focusOutside: root.focusBeforeHandbook
        }
    }

    // The pad on the screens (decisions E-583, E-584): its buttons, the ring around its focus and the
    // keyboard for text fields.
    Loader {
        objectName: "padHintsLoader"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        active: gameController.gamepad.active
        // In the world with the HUD hidden it hides with it; screens and the death screen keep their
        // hints.
        visible: !gameController.photo.hudHidden || gameController.overlay !== GameController.Overlay.None
                 || gameController.handbook.isOpen
                 || gameController.player.dead

        sourceComponent: PadHints {
            game: gameController
            // The chat line lies along the bottom edge.
            covered: ((keyboard.item as OnScreenKeyboard)?.visible ?? false) || chat.visible
        }
    }

    // A reached advancement (decision E-923) at the top right, below the time of day it would cover
    // (a status effect under it is covered for the toast's few seconds); made for the first one,
    // hidden with the HUD and while a screenshot is taken.
    Loader {
        id: advancementToast

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Ui.hudPx(8)
        anchors.topMargin: Ui.hudPx(40)
        active: false
        // A notice for playing in the world (decision E-927): not over the panels of a screen (nor
        // over the advancement tree, which shows the same and would lose its close button).
        visible: ((item as AdvancementToast)?.showing ?? false) && !gameController.photo.hudHidden
                 && !gameController.photo.capturing
                 && (gameController.hudShown === GameController.HudShown.All || gameController.hudShown === GameController.HudShown.AllButHint)

        sourceComponent: AdvancementToast {}
    }

    Connections {
        target: gameController.player

        function onAdvancementReached(advancement: var) {
            advancementToast.active = true;
            (advancementToast.item as AdvancementToast).add(advancement);
        }
    }

    // Leaving the world (or losing it) forgets the advancements still waiting for their toast.
    Connections {
        target: gameController

        function onPhaseChanged() {
            if (gameController.phase !== GameController.Phase.Playing) {
                (advancementToast.item as AdvancementToast)?.clear();
            }
        }
    }

    // The notices at the top, one under the other; both hide while a screenshot is taken, the pad's
    // with the HUD (decisions E-613, E-692).
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Ui.px(48)
        spacing: Ui.px(8)
        visible: !gameController.photo.capturing

        // A pad connected, disconnected or took over; made for the first notice.
        Loader {
            id: padNotice

            anchors.horizontalCenter: parent.horizontalCenter
            active: false
            visible: ((item as PadNotice)?.showing ?? false) && !gameController.photo.hudHidden

            sourceComponent: PadNotice {
                game: gameController
            }
        }

        // The screenshot key saved a picture or failed to; made for the first one.
        Loader {
            id: photoNotice

            anchors.horizontalCenter: parent.horizontalCenter
            active: false
            visible: (item as PhotoNotice)?.showing ?? false

            sourceComponent: PhotoNotice {}
        }

        // Adventure mode refused a mining or use (decision E-735); made for the first one.
        Loader {
            id: refusalNotice

            anchors.horizontalCenter: parent.horizontalCenter
            active: false
            visible: ((item as PhotoNotice)?.showing ?? false) && !gameController.photo.hudHidden

            sourceComponent: PhotoNotice {
                labelName: "refusalNotice"
            }
        }
    }

    Connections {
        target: gameController.player

        function onActionRefused() {
            refusalNotice.active = true;
            (refusalNotice.item as PhotoNotice).show(qsTr("Adventure mode: not allowed here"));
        }
    }

    Connections {
        id: photoEvents

        // The first hiding in a run says how to get the HUD back.
        property bool hudHintShown: false

        target: gameController.photo

        function onHudHiddenChanged() {
            if (!gameController.photo.hudHidden || photoEvents.hudHintShown) {
                return;
            }
            photoEvents.hudHintShown = true;
            photoNotice.active = true;
            (photoNotice.item as PhotoNotice).show(qsTr("HUD hidden – %1 to show").arg(gameController.settings.keys.labels.hideHud));
        }

        function onScreenshotSaved() {
            photoNotice.active = true;
            (photoNotice.item as PhotoNotice).show(qsTr("Screenshot saved: %1").arg(gameController.photo.lastScreenshot));
        }

        function onScreenshotFailed(reason: string) {
            photoNotice.active = true;
            (photoNotice.item as PhotoNotice).show(reason);
        }
    }

    Connections {
        target: gameController.gamepad.pads

        function onNoticeChanged() {
            padNotice.active = true;
        }
    }

    // The ring's place over the screens (offscreen runs move the screens with it).
    Item {
        id: ringHome

        anchors.fill: parent
        z: 1000

        Loader {
            active: gameController.gamepad.active

            sourceComponent: FocusRing {
                game: gameController
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
                game: gameController
                // The UI language picks the letters' layout, also after a switch at runtime.
                language: gameController.settings.contentLanguage
            }
        }
    }

    Connections {
        target: gameController.gamepad

        function onKeyboardRequested(field) {
            keyboard.active = true;
            (keyboard.item as OnScreenKeyboard).openFor(field);
        }

        // The keyboard or the mouse took over: the field is typed into directly.
        function onActiveChanged() {
            if (!gameController.gamepad.active && keyboard.item)
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
