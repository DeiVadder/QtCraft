// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// The dial of a held compass or clock (decision E-769), beside the hotbar: a compass needle (red
// end) towards the world spawn, or a clock hand with the time of day below the dial. The angle
// comes from HudState.dialAngle (degrees clockwise from the top).
Item {
    id: display

    required property HudState hudState

    objectName: "dialDisplay"
    visible: display.hudState.dial !== HudState.None
    width: Ui.hudPx(64)
    height: Ui.hudPx(64) + (clockLabel.visible ? clockLabel.height : 0)

    Rectangle {
        id: face

        width: Ui.hudPx(64)
        height: width
        radius: width / 2
        color: display.hudState.dial === HudState.Clock ? Theme.clockNight : Theme.dialFace
        border.color: display.hudState.dial === HudState.Clock ? Theme.clockRim : Theme.dialRim
        border.width: Ui.hudPx(4)

        // The clock's day half (sky blue, sun on top) turns with the hand.
        Rectangle {
            visible: display.hudState.dial === HudState.Clock
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.border.width
            width: parent.width - 2 * parent.border.width
            height: width / 2
            topLeftRadius: width / 2
            topRightRadius: width / 2
            color: Theme.clockDay
            transform: Rotation {
                origin.x: (face.width - 2 * face.border.width) / 2
                origin.y: (face.width - 2 * face.border.width) / 2
                angle: display.hudState.dialAngle
            }
        }

        // The needle or hand, from the centre outwards; the compass needle has a dark tail.
        Item {
            anchors.fill: parent
            rotation: display.hudState.dialAngle

            Rectangle {
                x: (parent.width - width) / 2
                y: parent.height * 0.14
                width: Ui.hudPx(5)
                height: parent.height * 0.36
                color: display.hudState.dial === HudState.Compass ? Theme.health : Theme.clockHand
            }
            Rectangle {
                visible: display.hudState.dial === HudState.Compass
                x: (parent.width - width) / 2
                y: parent.height * 0.5
                width: Ui.hudPx(5)
                height: parent.height * 0.36
                color: Theme.dialRim
            }
        }
    }

    HudLabel {
        id: clockLabel

        visible: display.hudState.dial === HudState.Clock
        anchors.horizontalCenter: face.horizontalCenter
        anchors.top: face.bottom
        font.pixelSize: Ui.hudPx(13)
        text: display.hudState.clockText
    }
}
