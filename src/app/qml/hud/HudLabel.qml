// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// A text over the world on a dark, semi-transparent backdrop, so it stays readable over snow,
// sand, water and the night sky (contrast checked in test_ui_palette). Plain text: chat lines and
// names come from other players and the server and must never load images or links. Sized with
// the HUD (Ui.hudPx, decision E-725).
Label {
    textFormat: Text.PlainText
    color: Theme.text
    leftPadding: Ui.hudPx(6)
    rightPadding: Ui.hudPx(6)
    topPadding: Ui.hudPx(2)
    bottomPadding: Ui.hudPx(2)
    font.pixelSize: Ui.hudPx(14)

    background: Rectangle {
        color: Theme.hudBackdrop
        radius: Ui.hudPx(3)
    }
}
