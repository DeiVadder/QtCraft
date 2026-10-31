// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// The background of the start and loading screens (decision E-943): the menu's gradient over the
// whole window, which takes the keyboard focus while shown and lets no click, hover or wheel through
// to the menu below. The screens on it add their content and their keys.
Rectangle {
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.menuTop }
        GradientStop { position: 1.0; color: Theme.menuBottom }
    }
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Component.onCompleted: if (visible) forceActiveFocus()

    MouseArea {
        anchors.fill: parent
        Accessible.ignored: true
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }
}
