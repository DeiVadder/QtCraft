// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The death screen (Menue-Pruefung TD-1): a card (WorldCard) over the reddened world, the title
// at the screen title's size, and the buttons of the pause menu, as wide as each other: "Respawn"
// (primary, where the pad's focus starts) and the way out (LeaveGameButton, secondary). It cannot
// be closed: Esc and B do nothing here.
Rectangle {
    id: screen

    required property GameController game
    // The height at the bottom the HUD's status rows take behind the screen (Main.qml); the card
    // stays above them.
    property real statusReserved: 0

    color: Theme.deathScrim
    // Takes the keyboard, so keys held when dying do nothing in the world below.
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab

    WorldCard {
        // Clear of the HUD's status rows behind it (decision E-927) and of the pad's hints.
        reservedBottom: Math.max(screen.statusReserved, screen.game.gamepad.focusShown ? Ui.size.padHints : 0)

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Ui.space.xs

            Label {
                Layout.fillWidth: true
                text: qsTr("You died")
                color: Theme.text
                font.pixelSize: Ui.type.title
                font.bold: true
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Your inventory is kept.")
                color: Theme.textMuted
                font.pixelSize: Ui.type.help
                wrapMode: Text.WordWrap
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Ui.space.s

            GameButton {
                objectName: "respawnButton"
                Layout.fillWidth: true
                kind: GameButton.Primary
                large: true
                text: qsTr("Respawn")
                onClicked: screen.game.player.respawn()
            }

            LeaveGameButton {
                Layout.fillWidth: true
                game: screen.game
            }
        }
    }
}
