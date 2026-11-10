// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The "About" page of the settings (decision E-1138): the game's version and licence, and the
// credit line of every music track exactly as its licence asks (from the tracks' descriptions).
SettingsPage {
    id: page

    required property GameController game

    title: qsTr("About")

    SettingGroup {
        title: qsTr("QtCraft %1").arg(Qt.application.version)

        Label {
            Layout.fillWidth: true
            text: qsTr("Free software under the GNU General Public License, version 3 or later. The handbook lists the music and its licences in the chapter \"Music\".")
            wrapMode: Text.WordWrap
            font.pixelSize: Ui.type.body
            color: Theme.textSecondary
        }
    }

    SettingGroup {
        title: qsTr("Music")

        Repeater {
            model: page.game.settings.audio.musicCredits

            Label {
                required property string modelData

                Layout.fillWidth: true
                text: modelData
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                font.pixelSize: Ui.type.body
                color: Theme.text
            }
        }

        // CC BY 4.0 asks that every copy names the licence with its address.
        Label {
            Layout.fillWidth: true
            text: qsTr("Tracks released under CC-BY 4.0 are licensed under the Creative Commons Attribution 4.0 International licence: %1. Its text comes with the game as CC-BY-4.0.txt.")
                  .arg("https://creativecommons.org/licenses/by/4.0/")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.pixelSize: Ui.type.body
            color: Theme.textSecondary
        }
    }
}
