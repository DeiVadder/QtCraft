// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// The discreet notice of a reached advancement (decision E-923): slides in at the top right with the
// advancement's icon, a heading by its frame ("Advancement made!", "Goal reached!", "Challenge
// complete!") and its title, stays for shownMs and slides out. Which advancement shows and which
// wait is the queue's (AdvancementQueue, decision E-954); this item only animates it and asks the
// queue for the next one when a toast has gone. Goals and challenges get the experience colour for
// the heading and the border.
Item {
    id: toast

    required property AdvancementQueue queue
    readonly property int shownMs: 5000
    readonly property int slideMs: 250
    readonly property var current: queue.current

    // Slides the queue's toast in and starts its time. For the first toast it may run twice (once
    // made, and once the queue notifies); the second run only restarts the same slide, which is
    // harmless.
    function slide() {
        if (!queue.showing)
            return;
        card.x = toast.width;
        slideIn.restart();
        timer.restart();
    }

    implicitWidth: card.width
    implicitHeight: card.height
    clip: true
    Component.onCompleted: slide()

    Connections {
        target: toast.queue

        function onCurrentChanged() {
            toast.slide();
        }
    }

    Rectangle {
        id: card

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
                    text: toast.current.heading ?? ""
                    color: toast.current.special ? Theme.experience : Theme.heading
                    font.pixelSize: Ui.hudPx(13)
                }

                Label {
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
        onFinished: toast.queue.next()
    }

    Timer {
        id: timer

        interval: toast.shownMs
        onTriggered: slideOut.restart()
    }
}
