// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// The keyboard focus of the game's controls (decision E-845): the same gold as the pad's ring
// (FocusRing), drawn just outside the control, never only a change of colour. Fills its parent.
Rectangle {
    anchors.fill: parent
    anchors.margins: -2 * Ui.size.focus
    color: Theme.clear
    border.color: Theme.focusRing
    border.width: Ui.size.focus
    radius: Ui.radius
}
