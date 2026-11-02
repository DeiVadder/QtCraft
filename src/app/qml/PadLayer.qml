// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// The pad on the screens (decisions E-583, E-584), over everything else of the window: its buttons
// along the bottom, the ring around its focus and the keyboard for text fields. They are made while
// the pad leads (the keyboard when a field first asks for it), so without a pad they cost nothing.
// An item of the content, so the keyboard opens in the window that shows the content (offscreen
// runs move it into another one; a popup of the window itself would stay behind).
Item {
    id: padLayer

    required property GameController game
    // The chat line is shown: it lies along the bottom edge, where the hints would be.
    property bool chatShown: false

    Loader {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        active: padLayer.game.gamepad.active
        // In the world with the HUD hidden it hides with it; screens and the death screen keep their
        // hints.
        visible: !padLayer.game.photo.hudHidden || padLayer.game.overlay !== GameController.Overlay.None
                 || padLayer.game.handbook.isOpen
                 || padLayer.game.player.dead

        sourceComponent: PadHints {
            game: padLayer.game
            covered: ((keyboard.item as OnScreenKeyboard)?.visible ?? false) || padLayer.chatShown
        }
    }

    // The ring's place over the screens (offscreen runs move the screens with it).
    Item {
        id: ringHome

        anchors.fill: parent
        z: 1000

        Loader {
            active: padLayer.game.gamepad.active

            sourceComponent: FocusRing {
                game: padLayer.game
                home: ringHome
            }
        }
    }

    Loader {
        id: keyboard

        active: false

        sourceComponent: OnScreenKeyboard {
            areaWidth: padLayer.width
            areaHeight: padLayer.height
            game: padLayer.game
            // The UI language picks the letters' layout, also after a switch at runtime.
            language: padLayer.game.settings.contentLanguage
        }
    }

    Connections {
        target: padLayer.game.gamepad

        function onKeyboardRequested(field) {
            keyboard.active = true;
            (keyboard.item as OnScreenKeyboard).openFor(field);
        }

        // The keyboard or the mouse took over: the field is typed into directly.
        function onActiveChanged() {
            if (!padLayer.game.gamepad.active && keyboard.item)
                (keyboard.item as OnScreenKeyboard).close();
        }
    }
}
