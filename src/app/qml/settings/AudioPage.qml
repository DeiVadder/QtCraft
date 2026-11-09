// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtCraft

// The audio page of the settings (decision E-845): the master volume with the mute switch right
// below it (muted, the volumes dim but stay adjustable), then music, effects and ambience; below
// them where the music comes from (decision E-1137) with the note about Content ID.
SettingsPage {
    id: page

    required property GameController game

    title: qsTr("Audio")

    SettingGroup {
        title: qsTr("Volume")

        SettingSlider {
            label: qsTr("Master volume")
            dimmed: page.game.settings.audio.muted
            from: 0
            to: 1
            value: page.game.settings.audio.masterVolume
            valueText: qsTr("%1 %").arg(Math.round(value * 100))
            onMoved: page.game.settings.audio.masterVolume = value
        }

        SettingSwitch {
            label: qsTr("Mute all sounds")
            checked: page.game.settings.audio.muted
            onToggled: page.game.settings.audio.muted = checked
        }

        SettingSlider {
            label: qsTr("Music")
            dimmed: page.game.settings.audio.muted
            from: 0
            to: 1
            value: page.game.settings.audio.musicVolume
            valueText: qsTr("%1 %").arg(Math.round(value * 100))
            onMoved: page.game.settings.audio.musicVolume = value
        }

        SettingSlider {
            label: qsTr("Effects")
            dimmed: page.game.settings.audio.muted
            from: 0
            to: 1
            value: page.game.settings.audio.effectsVolume
            valueText: qsTr("%1 %").arg(Math.round(value * 100))
            onMoved: page.game.settings.audio.effectsVolume = value
        }

        SettingSlider {
            label: qsTr("Ambience")
            dimmed: page.game.settings.audio.muted
            from: 0
            to: 1
            value: page.game.settings.audio.ambientVolume
            valueText: qsTr("%1 %").arg(Math.round(value * 100))
            onMoved: page.game.settings.audio.ambientVolume = value
        }
    }

    SettingGroup {
        title: qsTr("Soundtrack")

        SettingChoice {
            label: qsTr("Music")
            model: [
                { text: qsTr("Scott Buckley tracks"), value: AudioSettings.MusicSource.Tracks },
                { text: qsTr("Generated"), value: AudioSettings.MusicSource.Generated }
            ]
            currentValue: page.game.settings.audio.musicSource
            onActivated: value => page.game.settings.audio.musicSource = value
        }

        Hint {
            Layout.fillWidth: true
            text: qsTr("Videos or streams that contain these tracks may get a YouTube Content ID claim. If you record or stream, switch to generated music.")
        }
    }
}
