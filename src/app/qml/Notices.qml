// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// The short notices over the world and the screens: a reached advancement at the top right, and at
// the top the pad's notice, the photo mode's notice and adventure mode's refusal, one under the
// other. Each is made for its first notice.
Item {
    id: notices

    required property GameController game

    // One notice at the top, made for its first notice (`active` set then).
    component NoticeSlot: Loader {
        anchors.horizontalCenter: parent.horizontalCenter
        active: false
    }

    // A reached advancement (decision E-923) at the top right, below the time of day it would cover
    // (a status effect under it is covered for the toast's few seconds); hidden with the HUD and
    // while a screenshot is taken.
    Loader {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Ui.hudPx(8)
        anchors.topMargin: Ui.hudPx(40)
        // Made while a toast shows (the queue in C++ keeps the waiting ones).
        active: notices.game.player.advancementToasts.showing
        // A notice for playing in the world (decision E-927): not over the panels of a screen (nor
        // over the advancement tree, which shows the same and would lose its close button).
        visible: !notices.game.photo.hudHidden && !notices.game.photo.capturing && notices.game.hud.viewShown

        sourceComponent: AdvancementToast {
            queue: notices.game.player.advancementToasts
        }
    }

    // The notices at the top; all hide while a screenshot is taken, the pad's and the refusal with
    // the HUD (decisions E-613, E-692).
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Ui.px(48)
        spacing: Ui.px(8)
        visible: !notices.game.photo.capturing

        // A pad connected, disconnected or took over.
        NoticeSlot {
            id: padNotice

            visible: ((item as PadNotice)?.showing ?? false) && !notices.game.photo.hudHidden

            sourceComponent: PadNotice {
                game: notices.game
            }
        }

        // The screenshot key saved a picture or failed to, or the HUD was hidden.
        NoticeSlot {
            id: photoNotice

            visible: (item as Notice)?.showing ?? false

            sourceComponent: Notice {
                labelName: "photoNotice"
            }
        }

        // The game mode refused a mining or use (adventure mode, decision E-735).
        NoticeSlot {
            id: refusalNotice

            visible: ((item as Notice)?.showing ?? false) && !notices.game.photo.hudHidden

            sourceComponent: Notice {
                labelName: "refusalNotice"
            }
        }
    }

    Connections {
        target: notices.game.player

        function onActionRefused(text: string) {
            refusalNotice.active = true;
            (refusalNotice.item as Notice).show(text);
        }
    }

    Connections {
        target: notices.game.photo

        // The first hiding in a run says how to get the HUD back.
        function onHudHintWanted() {
            photoNotice.active = true;
            (photoNotice.item as Notice).show(qsTr("HUD hidden – %1 to show").arg(notices.game.settings.keys.labels.hideHud));
        }

        function onScreenshotSaved() {
            photoNotice.active = true;
            (photoNotice.item as Notice).show(qsTr("Screenshot saved: %1").arg(notices.game.photo.lastScreenshot));
        }

        function onScreenshotFailed(reason: string) {
            photoNotice.active = true;
            (photoNotice.item as Notice).show(reason);
        }
    }

    Connections {
        target: notices.game.gamepad.pads

        function onNoticeChanged() {
            padNotice.active = true;
        }
    }
}
