// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// The discreet notice of a reached advancement (decision E-923): slides in at the top right with the
// advancement's icon, a heading by its frame ("Advancement made!", "Goal reached!", "Challenge
// complete!") and its title, stays for shownMs and slides out; further ones wait in a queue of at
// most maxWaiting, and what goes beyond it is folded into one last "+N more" notice (granting
// everything by command reaches dozens at once). Goals and challenges get the experience colour for
// the heading and the border. clear() forgets the queue when the player leaves the world.
Item {
    id: toast

    readonly property int shownMs: 5000
    readonly property int slideMs: 250
    readonly property int maxWaiting: 5
    // The advancement on show ({name, title, description, icon, heading, special}), empty while none.
    property var current: ({})
    property var waiting: []
    readonly property bool showing: current.name !== undefined

    // Advancements beyond the queue, shown together as the last notice.
    property int folded: 0

    // Queues `advancement` (QcPlayerController::advancementReached).
    function add(advancement: var) {
        if (waiting.length < maxWaiting) {
            waiting = waiting.concat([advancement]);
        } else {
            folded += 1;
        }
        if (!showing) {
            next();
        }
    }

    // Forgets the advancement on show and the queue.
    function clear() {
        waiting = [];
        folded = 0;
        current = {};
        timer.stop();
        slideIn.stop();
        slideOut.stop();
    }

    function next() {
        if (waiting.length === 0 && folded === 0) {
            current = {};
            return;
        }
        if (waiting.length > 0) {
            current = waiting[0];
            waiting = waiting.slice(1);
        } else {
            current = {
                name: "more",
                title: qsTr("+%n more", "", folded),
                heading: qsTr("Advancements made!"),
                icon: "",
                special: false
            };
            folded = 0;
        }
        card.x = toast.width;
        slideIn.restart();
        timer.restart();
    }

    implicitWidth: card.width
    implicitHeight: card.height
    visible: showing
    clip: true

    Rectangle {
        id: card

        objectName: "advancementToast"
        width: Ui.hudPx(300)
        height: row.implicitHeight + Ui.hudPx(16)
        radius: Ui.hudPx(4)
        color: Theme.hudBackdrop
        border.color: toast.current.special ? Theme.experience : Theme.panelBorder
        border.width: Ui.hudPx(2)

        Row {
            id: row

            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Ui.hudPx(10)
            spacing: Ui.hudPx(10)

            Rectangle {
                width: Ui.hudPx(40)
                height: Ui.hudPx(40)
                radius: Ui.hudPx(4)
                color: Theme.iconBackdrop
                anchors.verticalCenter: parent.verticalCenter

                Image {
                    anchors.fill: parent
                    anchors.margins: Ui.hudPx(4)
                    source: toast.current.icon ?? ""
                    smooth: false
                    sourceSize: Qt.size(32, 32)
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: card.width - Ui.hudPx(70)
                spacing: Ui.hudPx(2)

                Label {
                    objectName: "advancementToastHeading"
                    text: toast.current.heading ?? ""
                    color: toast.current.special ? Theme.experience : Theme.heading
                    font.pixelSize: Ui.hudPx(13)
                }

                Label {
                    objectName: "advancementToastTitle"
                    width: parent.width
                    text: toast.current.title ?? ""
                    color: Theme.text
                    elide: Text.ElideRight
                    font.pixelSize: Ui.hudPx(15)
                }
            }
        }
    }

    NumberAnimation {
        id: slideIn

        target: card
        property: "x"
        to: 0
        duration: toast.slideMs
        easing.type: Easing.OutCubic
    }

    NumberAnimation {
        id: slideOut

        target: card
        property: "x"
        to: toast.width
        duration: toast.slideMs
        easing.type: Easing.InCubic
        onFinished: toast.next()
    }

    Timer {
        id: timer

        interval: toast.shownMs
        onTriggered: slideOut.restart()
    }
}
