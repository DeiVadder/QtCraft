// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtCraft

// Ten air bubbles above the food (decision E-955), emptying from the left like the food: shown
// with the eyes under water and until the breath is full again (PlayerController.airRow, one
// point per bubble).
Row {
    id: airBar

    required property GameController game

    spacing: Ui.hudPx(2)
    visible: airBar.game.player.airRow.shown
    layoutDirection: Qt.RightToLeft

    Repeater {
        model: airBar.game.player.airRow

        Item {
            id: bubble

            required property real fill

            width: Ui.hudPx(22)
            height: Ui.hudPx(22)

            Rectangle {
                anchors.centerIn: parent
                width: Ui.hudPx(16)
                height: Ui.hudPx(16)
                radius: width / 2
                // The bubbles count from the right; the left ones go first.
                visible: bubble.fill > 0
                color: Qt.alpha(Theme.air, 0.35)
                border.color: Theme.air
                border.width: Ui.hudPx(2)

                // The shine of the bubble.
                Rectangle {
                    x: Ui.hudPx(4)
                    y: Ui.hudPx(3)
                    width: Ui.hudPx(4)
                    height: Ui.hudPx(4)
                    radius: width / 2
                    color: Theme.text
                }
            }
        }
    }
}
