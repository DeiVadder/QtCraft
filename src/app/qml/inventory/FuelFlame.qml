// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// The flame of a machine: it shrinks as the current fuel burns down.
Rectangle {
    id: flame

    // The share of the fuel left (0..1).
    required property real share

    objectName: "flame"
    implicitWidth: Ui.size.flame
    implicitHeight: Ui.size.flame
    radius: Ui.radius
    color: Theme.slotBorder

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - 2 * Ui.space.xs
        height: (parent.height - Ui.space.xs) * flame.share
        radius: Ui.radius
        gradient: Gradient {
            GradientStop {
                position: 0
                color: Theme.flameCore
            }
            GradientStop {
                position: 1
                color: Theme.flame
            }
        }
    }
}
