import QtQuick
import QtCraft

// The audio page of the settings (decision E-845): the master volume with the mute switch right
// below it (muted, the volumes dim but stay adjustable), then music, effects and ambience.
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
}
