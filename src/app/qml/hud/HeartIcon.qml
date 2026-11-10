// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// A heart of the HUD: the empty outline, filled from the left by the share `filled` in `colour`.
Item {
    id: heart

    required property real filled
    required property color colour

    width: Ui.hudPx(22)
    height: Ui.hudPx(22)

    Text {
        anchors.fill: parent
        text: "♥"
        color: Theme.slotBorder
        font.pixelSize: Ui.hudPx(22)
        horizontalAlignment: Text.AlignHCenter
    }

    Item {
        width: parent.width * heart.filled
        height: parent.height
        clip: true

        Text {
            width: heart.width
            height: heart.height
            text: "♥"
            color: heart.colour
            style: Text.Outline
            styleColor: Theme.textOutline
            font.pixelSize: Ui.hudPx(22)
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
