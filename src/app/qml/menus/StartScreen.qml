// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The window's first picture while the game starts (decision E-943, GameController.starting): the
// logo on the menu's background and a running bar, so the window is never empty while the
// worlds, mods and content load. Takes no input (LoadingBackdrop) and goes once the menu is ready.
LoadingBackdrop {
    objectName: "startScreen"
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Starting QtCraft")
    // Keys and the pad wait until the menu is ready.
    Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.column)
        spacing: Ui.space.xl

        GameLogo {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
        }

        LoadingBar {
            Layout.fillWidth: true
            waiting: true
        }
    }
}
