// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The advanced page of the settings (decisions E-225 and E-845, like Factorio's F4): the level of
// the debug display and the visualisations drawn into the world, each on its own (SettingCheck: a
// multiple choice within one group), then the log and crash reports. The debug options key opens
// this page in the world directly. Stored with the settings.
SettingsPage {
    id: page

    required property GameController game

    title: qsTr("Advanced")

    SettingGroup {
        title: qsTr("Debug")

        SettingDropdown {
            objectName: "debugLevel"
            label: qsTr("Debug display")
            key: page.game.settings.keys.labels.debugOverlay
            model: [qsTr("Off"), qsTr("Compact: frame rate and frame time"), qsTr("Full: position, light, target, time breakdown")]
            currentIndex: page.game.settings.debug.level
            onActivated: index => page.game.settings.debug.level = index
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: Ui.space.s
            text: qsTr("Shown in the world")
            font.pixelSize: Ui.type.body
            color: Theme.textSecondary
        }

        SettingCheck {
            text: qsTr("Chunk and section borders")
            checked: page.game.settings.debug.chunkBorders
            onToggled: page.game.settings.debug.chunkBorders = checked
        }
        SettingCheck {
            text: qsTr("Hitboxes of creatures, players and items")
            checked: page.game.settings.debug.hitboxes
            onToggled: page.game.settings.debug.hitboxes = checked
        }
        SettingCheck {
            text: qsTr("Light values around the target (sky, block)")
            checked: page.game.settings.debug.lightValues
            onToggled: page.game.settings.debug.lightValues = checked
        }
        SettingCheck {
            text: qsTr("Where creatures are heading")
            checked: page.game.settings.debug.mobGoals
            onToggled: page.game.settings.debug.mobGoals = checked
        }
        SettingCheck {
            text: qsTr("Flash sections when they are meshed again")
            checked: page.game.settings.debug.remeshFlash
            onToggled: page.game.settings.debug.remeshFlash = checked
        }

        Hint {
            Layout.fillWidth: true
            Layout.bottomMargin: Ui.space.xs
            text: qsTr("%1 opens these options in the world.")
                      .arg(page.game.settings.keys.labels.debugOptions)
        }
    }

    DiagnosticsSection {
        game: page.game
    }
}
