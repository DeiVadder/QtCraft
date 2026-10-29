// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// The scroll bar of every scrolling screen: a handle and a track from the palette, shown for as
// long as the content does not fit, so a part that scrolls says so at a glance (the style's bar
// on macOS hides until the content moves). The mouse wheel scrolls the view it belongs to.
ScrollBar {
    id: bar

    policy: bar.size < 1.0 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    minimumSize: 0.1
    padding: Ui.size.line * 2

    contentItem: Rectangle {
        implicitWidth: Ui.size.scrollHandle
        implicitHeight: Ui.size.scrollHandle
        radius: Ui.size.scrollHandle / 2
        color: bar.pressed ? Theme.text : bar.hovered ? Theme.textSecondary : Theme.textMuted
    }

    background: Rectangle {
        implicitWidth: Ui.size.scrollBar
        implicitHeight: Ui.size.scrollBar
        radius: Ui.size.scrollBar / 2
        color: Theme.scrim
    }
}
