// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The game ended without the player leaving (decision E-261): the server stopped, kicked or
// refused the player, or the connection broke. Shows why; back to the menu or, in a network game,
// join the same server again. A card on ScreenFrame with the buttons of the death screen.
Rectangle {
    id: screen

    required property GameController game

    color: Theme.scrimStrong
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape)
            screen.game.leaveGame();
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    ScreenFrame {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: screen.game.gamepad.focusShown ? -Ui.size.padHints / 2 : 0
        width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.s)
        height: column.implicitHeight + 2 * Ui.space.xl

        ColumnLayout {
            id: column

            anchors.fill: parent
            anchors.margins: Ui.space.xl
            spacing: Ui.space.xl

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Ui.space.xs

                Label {
                    Layout.fillWidth: true
                    text: screen.game.multiplayer.networkGame ? qsTr("Disconnected from the server") : qsTr("The game ended")
                    color: Theme.text
                    font.pixelSize: Ui.type.title
                    font.bold: true
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Label {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: screen.game.statusText
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Ui.space.s

                GameButton {
                    objectName: "disconnectBackButton"
                    Layout.fillWidth: true
                    kind: GameButton.Primary
                    large: true
                    text: qsTr("Back to menu")
                    onClicked: screen.game.leaveGame()
                }

                GameButton {
                    objectName: "joinAgainButton"
                    Layout.fillWidth: true
                    large: true
                    visible: screen.game.multiplayer.networkGame && screen.game.multiplayer.lastAddress.length > 0
                    text: qsTr("Join again")
                    onClicked: {
                        screen.game.leaveGame();
                        screen.game.multiplayer.joinAgain();
                    }
                }
            }
        }
    }
}
