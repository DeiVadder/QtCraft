import QtQuick
import QtQuick.Layouts
import QtCraft

// A setting with two to four short choices side by side (decision E-845, docs/UI-Leitfaden.md
// 6.6): Segments in a setting row. Five or more choices, or long ones, are a SettingDropdown.
// `model` lists { text, value }; `activated(value)` follows a choice by the player.
SettingRow {
    id: setting

    property alias model: segments.model
    property alias currentValue: segments.currentValue

    signal activated(var value)

    Segments {
        id: segments

        Layout.fillWidth: true
        label: setting.label
        onActivated: value => setting.activated(value)
    }
}
