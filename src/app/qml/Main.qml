// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// The game's window (decision E-530): the world, the HUD, the screens, the notices and the pad's
// layer over each other. main() makes the game and hands it in as gameController.
ApplicationWindow {
    id: root

    required property GameController gameController
    // The item that had the focus before the handbook opened; the handbook gives the focus back to
    // it. Kept here rather than read when the handbook opens: the screens it covers hide at the same
    // moment (and lose the focus) in no fixed order.
    property Item focusBeforeHandbook: null

    width: 1280
    height: 720
    // Every screen fits from this size on (the inventory, the character screen; decision E-231).
    minimumWidth: 1024
    minimumHeight: 640
    // Shown by main() once the screen is chosen.
    visible: false
    title: qsTr("QtCraft")
    color: Theme.menuBottom

    onActiveFocusItemChanged: {
        if (!root.gameController.handbook.isOpen && activeFocusItem)
            focusBeforeHandbook = activeFocusItem;
    }

    // The world the game draws into, and the world's input while it plays.
    Binding {
        target: root.gameController
        property: "view"
        value: worldView
    }
    Binding {
        target: root.gameController
        property: "inputEnabled"
        value: root.gameController.phase === GameController.Phase.Playing
    }
    // The UI scale (decision E-725): the player's choice and the window; Ui.px() and
    // Ui.hudPx() take their factors from here.
    Binding {
        target: UiScaling
        property: "setting"
        value: root.gameController.settings.uiScale
    }
    Binding {
        target: UiScaling
        property: "windowSize"
        value: Qt.size(root.width, root.height)
    }
    // The chat lines of the HUD all show while the chat line is open and fade while it is closed
    // (decision E-875).
    Binding {
        target: root.gameController.hud.chat
        property: "open"
        value: root.gameController.overlay === GameController.Overlay.Chat
    }
    // Controls (buttons, boxes, labels, tooltips, popups) inherit the window's font.
    WindowFont {
        target: root
    }

    WorldView {
        id: worldView

        anchors.fill: parent
        visible: root.gameController.phase === GameController.Phase.Playing
                 || root.gameController.phase === GameController.Phase.Loading
    }

    // The photo mode's key hides it with the crosshair and every overlay (decision E-692); nothing
    // of it shows behind a full-screen screen (decision E-927), the rest hides by GameController.hudShown
    // (behind the death screen only the status rows stay).
    Hud {
        id: worldHud

        anchors.fill: parent
        visible: root.gameController.phase === GameController.Phase.Playing && !root.gameController.photo.hudHidden
                 && root.gameController.hudShown !== GameController.HudShown.Nothing
        game: root.gameController
    }

    // While its pad button or its key is held in the world, not with the HUD hidden (decisions E-581,
    // E-614, E-692). The pad's items exist only while the pad leads (the keyboard from its first use
    // on), so without a pad they cost nothing.
    Loader {
        anchors.centerIn: parent
        active: (root.gameController.gamepad.active || root.gameController.gamepad.radialOpen || root.gameController.input.radialOpen)
                && root.gameController.phase === GameController.Phase.Playing
        visible: root.gameController.phase === GameController.Phase.Playing
                 && (root.gameController.gamepad.radialOpen || root.gameController.input.radialOpen)
                 && !root.gameController.photo.hudHidden

        sourceComponent: HotbarRadial {
            game: root.gameController
        }
    }

    Screens {
        id: screens

        anchors.fill: parent
        game: root.gameController
        hud: worldHud
        focusBeforeHandbook: root.focusBeforeHandbook
    }

    Notices {
        anchors.fill: parent
        game: root.gameController
    }

    PadLayer {
        anchors.fill: parent
        game: root.gameController
        chatShown: screens.chatShown
    }

    // Cmd+W (elsewhere Ctrl+W or Ctrl+F4) closes the window like its close button: the world is
    // saved on the way out. In the world the game leaves Cmd combinations to the system.
    Shortcut {
        sequences: [StandardKey.Close]
        onActivated: root.close()
    }
}
