// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// The audio page of the settings (decision E-845): the master volume with the mute switch right
// below it (muted, the volumes dim but stay adjustable), then one volume per sound category
// (decision E-1150); below them where the music comes from (decision E-1137) with the note about
// Content ID.
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

        // One slider per sound category (decision E-1150), in the order of the role model.
        Repeater {
            model: page.game.settings.audio.categories

            delegate: SettingSlider {
                id: categorySlider

                // "categoryVolume<category>" for tests (test_audio_page).
                objectName: "categoryVolume" + categorySlider.category

                required property int category
                required property string name
                required property real volume
                required property real defaultVolume

                label: categorySlider.name
                dimmed: page.game.settings.audio.muted
                from: 0
                to: 1
                value: categorySlider.volume
                defaultValue: categorySlider.defaultVolume
                valueText: qsTr("%1 %").arg(Math.round(categorySlider.value * 100))
                defaultText: qsTr("%1 %").arg(Math.round(categorySlider.defaultVolume * 100))
                onMoved: page.game.settings.audio.setVolume(categorySlider.category, categorySlider.value)
            }
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
