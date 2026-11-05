// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A setting chosen from a list (decision E-845, docs/UI-Leitfaden.md 6.7): a ThemedComboBox, a
// sunken field with the choice and an arrow; the list opens with a click or the pad's confirm. For
// languages, screens, frame rate limits, the interface size and controller profiles. `activated(index)` follows a
// choice by the player; currentValue is the chosen value.
SettingRow {
    id: setting

    property alias model: box.model
    property alias textRole: box.textRole
    property alias valueRole: box.valueRole
    property alias currentIndex: box.currentIndex
    property alias count: box.count
    readonly property alias currentValue: box.currentValue
    // The text shown for the choice (by default the chosen entry's).
    property alias displayText: box.displayText
    // The list of choices (the test hook "open:<objectName>" opens it).
    readonly property alias popup: box.popup
    // The value shown as chosen (of valueRole), when set: also after a reset of what it follows
    // and once the list is filled (the player's choice would break a plain binding of currentIndex).
    property var shownValue

    signal activated(int index)

    function indexOfValue(value: var): int {
        return box.indexOfValue(value);
    }

    ThemedComboBox {
        id: box

        Layout.fillWidth: true
        Accessible.name: setting.label
        onActivated: index => setting.activated(index)
    }
    Binding {
        when: setting.shownValue !== undefined
        target: box
        property: "currentIndex"
        value: box.count > 0 ? box.indexOfValue(setting.shownValue) : -1
    }
}
