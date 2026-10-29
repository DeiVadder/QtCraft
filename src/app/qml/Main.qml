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
    // The item that had the focus before the handbook opened; the handbook gives the focus back to
    // it. Kept here rather than read when the handbook opens: the screens it covers hide at the same
    // moment (and lose the focus) in no fixed order.
    property Item focusBeforeHandbook: null
    // Where the advancements lead back to: the pause menu when they opened from it, else the world.
    property int advancementsReturn: GameController.Overlay.None
    property int lastOverlay: GameController.Overlay.None

    onActiveFocusItemChanged: {
        if (!game.handbook.isOpen && activeFocusItem)
            focusBeforeHandbook = activeFocusItem;
    }

    // The UI scale (decision E-725): the player's choice and the window; Ui.px() and
    // Ui.hudPx() take their factors from here.
    Binding {
        target: UiScaling
        property: "setting"
        value: game.settings.uiScale
    }
    Binding {
        target: UiScaling
        property: "windowSize"
        value: Qt.size(root.width, root.height)
    }
    // The chat lines of the HUD all show while the chat line is open and fade while it is closed
    // (decision E-875).
    Binding {
        target: game.hud.chat
        property: "open"
        value: game.overlay === GameController.Overlay.Chat
    }
    // Controls (buttons, boxes, labels, tooltips, popups) inherit the window's font.
    WindowFont {
        target: root
    }

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

    // The photo mode's key hides it with the crosshair and every overlay (decision E-692); nothing
    // of it shows behind a full-screen screen (decision E-927), the rest hides by GameController.hudShown
    // (behind the death screen only the status rows stay).
    Hud {
        id: hud

        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && !game.photo.hudHidden
                 && game.hudShown !== GameController.HudShown.Nothing
        game: game
    }

    // While its pad button or its key is held in the world, not with the HUD hidden (decisions E-581,
    // E-614, E-692). The pad's items exist only while the pad leads (the keyboard from its first use
    // on), so without a pad they cost nothing.
    Loader {
        anchors.centerIn: parent
        active: (game.gamepad.active || game.gamepad.radialOpen || game.input.radialOpen) && game.phase === GameController.Phase.Playing
        visible: game.phase === GameController.Phase.Playing && (game.gamepad.radialOpen || game.input.radialOpen)
                 && !game.photo.hudHidden

        sourceComponent: HotbarRadial {
            game: root.controller
        }
    }

    InventoryScreen {
        anchors.fill: parent
        visible: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Inventory
        game: game
    }

    // The other screens exist only while shown and for a while after (LazyScreen, decision E-691).
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Creative
        // About 30 ms to make in Release (E-690): made with the creative world, not on the first E.
        preload: game.phase === GameController.Phase.Playing && game.player.creative

        screen: CreativeScreen {
            game: root.controller
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Furnace

        screen: FurnaceScreen {
            game: root.controller
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Chest

        screen: ChestScreen {
            game: root.controller
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Trade

        screen: TradeScreen {
            game: root.controller
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Stonecutter

        screen: StonecutterScreen {
            game: root.controller
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Enchanting

        screen: EnchantingScreen {
            game: root.controller
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Anvil

        screen: AnvilScreen {
            game: root.controller
        }
    }

    // With its sub-screens: settings, character and open to LAN.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing
               && [GameController.Overlay.Pause, GameController.Overlay.Settings, GameController.Overlay.Display, GameController.Overlay.Audio,
                   GameController.Overlay.Interface, GameController.Overlay.Controls,
                   GameController.Overlay.Gamepad, GameController.Overlay.Debug, GameController.Overlay.TexturePacks,
                   GameController.Overlay.Character, GameController.Overlay.Lan, GameController.Overlay.Admin].includes(game.overlay)

        screen: PauseMenu {
            game: root.controller
            statusReserved: hud.visible ? hud.height - hud.statusArea.y : 0
        }
    }

    // The tree of the adventure guide (decision E-924).
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Advancements

        screen: AdvancementsScreen {
            game: root.controller
            onCloseRequested: root.controller.overlay = root.advancementsReturn
        }
    }

    Connections {
        target: game

        function onOverlayChanged() {
            if (game.overlay === GameController.Overlay.Advancements)
                root.advancementsReturn = root.lastOverlay === GameController.Overlay.Pause ? GameController.Overlay.Pause : GameController.Overlay.None;
            root.lastOverlay = game.overlay;
        }
    }

    // While the player list key is held; over the world and the HUD, under the screens.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.None && game.input.playerListHeld
               && !game.photo.hudHidden

        screen: PlayerListOverlay {
            game: root.controller
        }
    }

    ChatOverlay {
        id: chat

        anchors.fill: parent
        // Same frame as the HUD (both fill the window): the /help hint stays clear of the hotbar.
        statusArea: hud.visible ? hud.statusArea : Qt.rect(width, height, 0, 0)
        visible: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Chat
        game: game
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.overlay === GameController.Overlay.Sign

        screen: SignEditor {
            game: root.controller
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Playing && game.player.dead

        screen: DeathScreen {
            game: root.controller
            statusReserved: hud.visible ? hud.height - hud.statusArea.y : 0
        }
    }

    // Only in the menu phase: in the world its objects are given back once the keep-alive time ran out.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Menu
        // Made at once (decision E-875): it is the first screen at start-up, where test hooks and
        // the pad act on it right away, and when a world closes nothing plays that a stall could cut.
        asynchronous: false

        screen: MainMenu {
            game: root.controller
            onQuitRequested: Qt.quit()
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Loading || game.phase === GameController.Phase.Error

        screen: MessageScreen {
            text: root.controller.statusText
            // Loading a network server's world can be cancelled.
            showBackButton: root.controller.phase === GameController.Phase.Error || root.controller.multiplayer.networkGame
            backText: root.controller.phase === GameController.Phase.Error ? qsTr("Back to menu") : qsTr("Cancel")
            onBackRequested: root.controller.leaveGame()
        }
    }

    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.phase === GameController.Phase.Disconnected

        screen: DisconnectScreen {
            game: root.controller
        }
    }

    // Over everything else, so it opens from the menu, the world and every in-game screen.
    LazyScreen {
        anchors.fill: parent
        keepAliveMs: root.controller.screenKeepAliveMs
        logCreation: root.controller.logScreenCreation
        shown: game.handbook.isOpen

        screen: HandbookScreen {
            game: root.controller
            focusOutside: root.focusBeforeHandbook
        }
    }

    // The pad on the screens (decisions E-583, E-584): its buttons, the ring around its focus and the
    // keyboard for text fields.
    Loader {
        objectName: "padHintsLoader"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        active: game.gamepad.active
        // In the world with the HUD hidden it hides with it; screens and the death screen keep their
        // hints.
        visible: !game.photo.hudHidden || game.overlay !== GameController.Overlay.None || game.handbook.isOpen
                 || game.player.dead

        sourceComponent: PadHints {
            game: root.controller
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
        visible: ((item as AdvancementToast)?.showing ?? false) && !game.photo.hudHidden && !game.photo.capturing
                 && (game.hudShown === GameController.HudShown.All || game.hudShown === GameController.HudShown.AllButHint)

        sourceComponent: AdvancementToast {}
    }

    Connections {
        target: game.player

        function onAdvancementReached(advancement: var) {
            advancementToast.active = true;
            (advancementToast.item as AdvancementToast).add(advancement);
        }
    }

    // Leaving the world (or losing it) forgets the advancements still waiting for their toast.
    Connections {
        target: game

        function onPhaseChanged() {
            if (game.phase !== GameController.Phase.Playing) {
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
        visible: !game.photo.capturing

        // A pad connected, disconnected or took over; made for the first notice.
        Loader {
            id: padNotice

            anchors.horizontalCenter: parent.horizontalCenter
            active: false
            visible: ((item as PadNotice)?.showing ?? false) && !game.photo.hudHidden

            sourceComponent: PadNotice {
                game: root.controller
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
            visible: ((item as PhotoNotice)?.showing ?? false) && !game.photo.hudHidden

            sourceComponent: PhotoNotice {
                labelName: "refusalNotice"
            }
        }
    }

    Connections {
        target: game.player

        function onActionRefused() {
            refusalNotice.active = true;
            (refusalNotice.item as PhotoNotice).show(qsTr("Adventure mode: not allowed here"));
        }
    }

    Connections {
        id: photoEvents

        // The first hiding in a run says how to get the HUD back.
        property bool hudHintShown: false

        target: game.photo

        function onHudHiddenChanged() {
            if (!game.photo.hudHidden || photoEvents.hudHintShown) {
                return;
            }
            photoEvents.hudHintShown = true;
            photoNotice.active = true;
            (photoNotice.item as PhotoNotice).show(qsTr("HUD hidden – %1 to show").arg(game.settings.keys.labels.hideHud));
        }

        function onScreenshotSaved() {
            photoNotice.active = true;
            (photoNotice.item as PhotoNotice).show(qsTr("Screenshot saved: %1").arg(game.photo.lastScreenshot));
        }

        function onScreenshotFailed(reason: string) {
            photoNotice.active = true;
            (photoNotice.item as PhotoNotice).show(reason);
        }
    }

    Connections {
        target: game.gamepad.pads

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
                buttonTexts: root.controller.gamepad.pads.texts
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
