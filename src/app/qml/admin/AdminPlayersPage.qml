// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The players of "Manage server" (decision E-863): everyone playing, with ping, game mode, rights
// and address; operator rights are given and taken at once, kicking and banning ask first with a
// reason the player reads (AdminActionDialog, modal). The operator's own row (the host's, or his
// own on a server) has no actions, only a note who it is (decision E-931).
SettingsPage {
    id: page

    required property AdminController admin
    // The screen's modal dialog for actions that ask first.
    required property AdminActionDialog dialog

    // The host plays in the server's process: no round trip and no address to show.
    function details(player: var): string {
        if (player.local)
            return player.modeName;
        const parts = [player.ping < 0 ? qsTr("Ping unknown") : qsTr("Ping %1 ms").arg(player.ping), player.modeName];
        if (player.address.length > 0)
            parts.push(player.address);
        return parts.join(" · ");
    }

    // The row of the operator looking at the screen: he neither kicks nor bans himself. Only the
    // screen protects him; the server refuses actions on the host alone (decision E-931).
    function isOwn(player: var): bool {
        // isOwnName() is a call: the own name is read for the binding.
        void page.admin.ownName;
        return page.admin.isOwnName(player.name);
    }

    title: qsTr("Players")

    // The rows lie on the page as in the other lists of rows (worlds, texture packs), not on a card:
    // on the card's surface they would run into each other (UI-Leitfaden 6.9).
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Ui.space.s

        Label {
            Layout.fillWidth: true
            text: qsTr("Playing now (%n)", "", page.admin.players.length)
            font.pixelSize: Ui.type.group
            font.bold: true
            color: Theme.heading
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Label {
            Layout.fillWidth: true
            visible: page.admin.players.length === 0
            text: qsTr("Nobody is playing. The list fills as players join.")
            font.pixelSize: Ui.type.body
            color: Theme.textMuted
            wrapMode: Text.Wrap
        }

        Repeater {
            model: page.admin.players

            delegate: ListRow {
                id: row

                required property var modelData
                required property int index
                readonly property bool own: page.isOwn(modelData)

                title: modelData.operator ? qsTr("%1 · operator").arg(modelData.name) : modelData.name
                subtitle: page.details(modelData)
                actions: [
                    Label {
                        // The host's row has no actions: the server refuses them on him.
                        objectName: row.own ? "adminOwnHost" : "adminOtherHost"
                        Layout.alignment: Qt.AlignVCenter
                        visible: row.modelData.local
                        text: row.own ? qsTr("You host the world") : qsTr("Hosts the world")
                        font.pixelSize: Ui.type.help
                        color: Theme.textSecondary
                    },
                    RowLayout {
                        visible: !row.modelData.local
                        spacing: Ui.space.xs

                        GameButton {
                            objectName: "adminOp" + row.index
                            text: row.modelData.operator ? qsTr("Take rights") : qsTr("Make operator")
                            onClicked: row.modelData.operator ? page.dialog.takeRights(page.admin, row.modelData.name) : page.admin.op(row.modelData.name)
                        }

                        // Nobody kicks or bans himself; taking his own rights asks first.
                        GameButton {
                            objectName: "adminKick" + row.index
                            visible: !row.own
                            kind: GameButton.Destructive
                            text: qsTr("Kick …")
                            onClicked: page.dialog.ask(qsTr("Kick %1").arg(row.modelData.name),
                                                    qsTr("%1 leaves the server now and may join again.").arg(row.modelData.name),
                                                    qsTr("Kick"), true, reason => page.admin.kick(row.modelData.name, reason))
                        }
                        GameButton {
                            objectName: "adminBan" + row.index
                            visible: !row.own
                            kind: GameButton.Destructive
                            text: qsTr("Ban …")
                            onClicked: page.dialog.ask(qsTr("Ban %1").arg(row.modelData.name),
                                                    qsTr("%1 leaves the server now and may not join again until unbanned on the Lists page.").arg(row.modelData.name),
                                                    qsTr("Ban"), true, reason => page.admin.ban(row.modelData.name, reason))
                        }
                    }
                ]
            }
        }
    }
}
