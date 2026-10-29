// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The server's lists in "Manage server" (decision E-863): the whitelist (on or off, its names), the
// operators and the banned players. Every change goes to the server at once (as /whitelist, /op,
// /deop and /pardon would), and the lists show what it answers.
SettingsPage {
    id: page

    required property AdminController admin
    // The screen's modal dialog: taking one's own rights asks first.
    required property AdminActionDialog dialog
    readonly property var lists: admin.lists

    // A name typed into `field` goes to `add`; the field empties.
    function addName(field: GameTextField, add: var) {
        const name = field.text.trim();
        if (name.length > 0) {
            add(name);
            field.text = "";
        }
    }

    title: qsTr("Lists")

    SettingGroup {
        title: qsTr("Whitelist")

        SettingSwitch {
            objectName: "adminWhitelistSwitch"
            label: qsTr("Only listed players may join")
            help: qsTr("Operators may always join.")
            checked: page.lists.whitelistEnabled ?? false
            onToggled: page.admin.setWhitelist(checked)
        }
        Label {
            Layout.fillWidth: true
            visible: (page.lists.whitelist ?? []).length === 0
            text: qsTr("Nobody is on the whitelist yet. Add a name below.")
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
        Repeater {
            model: page.lists.whitelist ?? []

            delegate: SettingRow {
                id: listed

                required property string modelData

                label: modelData
                GameButton {
                    kind: GameButton.Quiet
                    text: qsTr("Remove")
                    Accessible.name: qsTr("Remove %1 from the whitelist").arg(listed.modelData)
                    onClicked: page.admin.whitelistRemove(listed.modelData)
                }
            }
        }
        SettingRow {
            label: qsTr("Add a player")

            GameTextField {
                id: whitelistField

                objectName: "adminWhitelistField"
                Layout.fillWidth: true
                maximumLength: page.admin.nameLength
                validator: RegularExpressionValidator { regularExpression: /\S*/ }
                placeholderText: qsTr("Name")
                Accessible.name: qsTr("Name to add to the whitelist")
                onAccepted: page.addName(whitelistField, name => page.admin.whitelistAdd(name))
            }
            GameButton {
                text: qsTr("Add")
                enabled: whitelistField.text.trim().length > 0
                onClicked: page.addName(whitelistField, name => page.admin.whitelistAdd(name))
            }
        }
    }

    SettingGroup {
        title: qsTr("Operators")

        Label {
            Layout.fillWidth: true
            visible: (page.lists.operators ?? []).length === 0
            text: qsTr("No operators in the server's list. The host of a world is always one.")
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
        Repeater {
            model: page.lists.operators ?? []

            delegate: SettingRow {
                id: operatorRow

                required property string modelData

                label: modelData
                GameButton {
                    kind: GameButton.Quiet
                    text: qsTr("Take rights")
                    Accessible.name: qsTr("Take the operator rights of %1").arg(operatorRow.modelData)
                    onClicked: page.dialog.takeRights(page.admin, operatorRow.modelData)
                }
            }
        }
        SettingRow {
            label: qsTr("Make operator")

            GameTextField {
                id: operatorField

                objectName: "adminOperatorField"
                Layout.fillWidth: true
                maximumLength: page.admin.nameLength
                validator: RegularExpressionValidator { regularExpression: /\S*/ }
                placeholderText: qsTr("Name")
                Accessible.name: qsTr("Name of the new operator")
                onAccepted: page.addName(operatorField, name => page.admin.op(name))
            }
            GameButton {
                text: qsTr("Add")
                enabled: operatorField.text.trim().length > 0
                onClicked: page.addName(operatorField, name => page.admin.op(name))
            }
        }
    }

    SettingGroup {
        title: qsTr("Banned")

        Label {
            Layout.fillWidth: true
            visible: (page.lists.bans ?? []).length === 0
            text: qsTr("Nobody is banned. Ban players on the Players page.")
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }
        Repeater {
            model: page.lists.bans ?? []

            delegate: SettingRow {
                id: banRow

                required property var modelData

                label: modelData.name
                help: modelData.reason.length > 0 ? qsTr("“%1”, by %2").arg(modelData.reason).arg(modelData.by)
                                                  : qsTr("By %1").arg(modelData.by)
                GameButton {
                    kind: GameButton.Quiet
                    text: qsTr("Unban")
                    Accessible.name: qsTr("Unban %1").arg(banRow.modelData.name)
                    onClicked: page.admin.unban(banRow.modelData.name)
                }
            }
        }
    }
}
