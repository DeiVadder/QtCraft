// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// The boss bars at the top of the screen (decision E-1127, HudState.bossBars): the name of each
// boss above a bar that shows its health in the colour the server chose, one under the other.
Column {
    id: bars

    required property HudState hudState

    spacing: Ui.hudPx(6)

    Repeater {
        model: bars.hudState.bossBars

        delegate: Column {
            id: bar

            required property string name
            required property real progress
            required property color barColour

            spacing: Ui.hudPx(2)

            HudLabel {
                anchors.horizontalCenter: parent.horizontalCenter
                text: bar.name
            }

            Rectangle {
                width: Ui.hudPx(364)
                height: Ui.hudPx(10)
                color: Theme.hudBackdrop
                border.color: Theme.textOutline
                border.width: Ui.hudPx(1)
                radius: Ui.hudPx(2)

                Rectangle {
                    x: parent.border.width
                    y: parent.border.width
                    height: parent.height - 2 * parent.border.width
                    width: Math.max(0, (parent.width - 2 * parent.border.width) * bar.progress)
                    color: bar.barColour
                    radius: Ui.hudPx(1)
                }
            }
        }
    }
}
