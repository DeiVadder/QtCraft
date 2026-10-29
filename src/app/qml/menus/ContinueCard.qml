// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The card at the top of the main menu's start page (variant C, decision E-852): the world played
// last with its name, when it was played, its type and mode, and "Continue" (the screen's primary
// action, where the pad's focus starts). Without a world that can be opened it welcomes the player
// and offers to create one instead.
Rectangle {
    id: card

    // The entry of GameController.library.worlds to continue, or null.
    property var world: null
    // What the card says about the world (WorldTexts.summary).
    property string summary
    // Whether there are saved worlds at all (none of them can be opened if `world` is null).
    property bool hasWorlds: false

    signal continueRequested(string folder)
    signal newWorldRequested

    implicitHeight: layout.implicitHeight + 2 * Ui.space.l
    radius: Ui.radius
    color: Theme.scrim

    Bevel {}

    RowLayout {
        id: layout

        x: Ui.space.l
        y: Ui.space.l
        width: parent.width - 2 * Ui.space.l
        spacing: Ui.space.l

        WorldTile {
            visible: card.world !== null
            name: card.world?.name ?? ""
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            spacing: Ui.space.xs

            Label {
                Layout.fillWidth: true
                text: card.world !== null ? qsTr("Last played") : qsTr("Welcome!")
                font.pixelSize: card.world !== null ? Ui.type.badge : Ui.type.group
                font.bold: true
                font.capitalization: card.world !== null ? Font.AllUppercase : Font.MixedCase
                color: Theme.heading
            }
            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: card.world !== null ? card.world.name
                      : card.hasWorlds ? qsTr("None of your worlds can be opened.") : qsTr("Create your first world.")
                font.pixelSize: Ui.type.title
                font.bold: true
                color: Theme.text
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: card.world !== null ? card.summary
                      : qsTr("Choose a name, a type of world and a game mode; the world is saved as you play.")
                font.pixelSize: Ui.type.help
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
        }

        GameButton {
            objectName: card.world !== null ? "continueButton" : "firstWorldButton"
            kind: GameButton.Primary
            large: true
            text: card.world !== null ? qsTr("Continue") : qsTr("Create a new world")
            Accessible.name: card.world !== null ? qsTr("Continue %1").arg(card.world.name) : text
            onClicked: card.world !== null ? card.continueRequested(card.world.folder) : card.newWorldRequested()
        }
    }
}
