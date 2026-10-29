// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The player's profile at the foot of the main menu's side bar (decision E-231, variant C of
// docs/UI-Leitfaden.md): a small live figure of the character as others will see it and the name
// the game joins network games with; a click (Enter, the pad's A) opens the character screen.
AbstractButton {
    id: card

    required property GameController game
    // The name the game joins network games with (set in the multiplayer screen, decision E-258).
    readonly property string playerName: game.multiplayer.settings.playerName.length > 0 ? game.multiplayer.settings.playerName : qsTr("Player")
    // Another screen covers the card: its preview stops moving.
    property bool covered: false

    Layout.fillWidth: true
    leftPadding: Ui.space.l
    rightPadding: Ui.space.s
    topPadding: Ui.space.xs
    bottomPadding: Ui.space.xs
    focusPolicy: Qt.StrongFocus
    // Disabled (the main menu's side bar during a join) it dims.
    opacity: enabled ? 1 : Ui.disabledOpacity
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("%1, customize character").arg(card.playerName)
    Accessible.description: qsTr("Your character as other players see it")
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        color: card.hovered ? Theme.rowHighlight : Theme.clear

        FocusOutline {
            visible: card.visualFocus
        }
    }
    contentItem: RowLayout {
        spacing: Ui.space.m

        CharacterPreviewHost {
            Layout.preferredWidth: Ui.size.figureWidth
            Layout.preferredHeight: Ui.size.figureHeight
            previewName: "profilePreview"
            source: card.game.preview
            // A gentle idle motion is enough in the menu.
            framesPerSecond: 15
            paused: card.covered
            interactive: false
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Ui.space.xs

            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: card.playerName
                font.pixelSize: Ui.type.body
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Customize character")
                font.pixelSize: Ui.type.help
                color: Theme.textMuted
                elide: Text.ElideRight
            }
        }
    }
}
