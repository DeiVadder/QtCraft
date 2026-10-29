// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The game rules and server limits in "Manage server" (decisions E-848, E-863), grouped as the
// server sends them, each with its translated name, key, description, range and default. Edits make
// a draft (docs/UI-Leitfaden.md 5, pattern 2), kept by the controller (AdminController.draft) so it
// outlives the screen closing: "Save" sends each changed value to the server (as /gamerule and
// /limits set would; a refused one stays changed), "Discard" drops them; "Default" sets one value
// back in the draft. The screen asks before leaving with unsaved changes.
ColumnLayout {
    id: page

    required property AdminController admin
    readonly property var draft: admin.draft
    readonly property bool dirty: admin.dirty

    function valueOf(setting: var): real {
        const id = (setting.limit ? "limit:" : "rule:") + setting.key;
        return draft[id] !== undefined ? draft[id] : setting.value;
    }

    function change(setting: var, value: real) {
        admin.changeSetting(setting.limit, setting.key, value);
    }

    // False when `text` is no value of `setting` (the draft keeps what it had).
    function changeText(setting: var, text: string): bool {
        return admin.changeSettingText(setting.limit, setting.key, text);
    }

    spacing: Ui.space.m

    SettingsPage {
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: qsTr("Game rules")

        Hint {
            Layout.fillWidth: true
            text: page.admin.limitsFromServer ? qsTr("Saved limits go to the server's server.json and apply at once.")
                                              : qsTr("Saved changes apply to all players at once; the world keeps them when it saves.")
        }

        Repeater {
            model: page.admin.ruleGroups.concat(page.admin.limitGroups)

            delegate: SettingGroup {
                id: group

                required property var modelData

                title: modelData.title

                Repeater {
                    model: group.modelData.settings

                    delegate: AdminSettingRow {
                        settings: page
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.space.s

        Label {
            Layout.fillWidth: true
            visible: page.dirty
            text: qsTr("Unsaved changes")
            font.pixelSize: Ui.type.body
            color: Theme.warning
        }
        Item {
            Layout.fillWidth: true
            visible: !page.dirty
        }
        GameButton {
            objectName: "adminSettingsDiscard"
            text: qsTr("Discard")
            enabled: page.dirty
            onClicked: page.admin.discardSettings()
        }
        GameButton {
            objectName: "adminSettingsSave"
            kind: GameButton.Primary
            text: qsTr("Save")
            enabled: page.dirty
            onClicked: page.admin.saveSettings()
        }
    }
}
