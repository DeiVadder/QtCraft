pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The players of "Manage server" (decision E-863): everyone playing, with ping, game mode, rights
// and address; operator rights are given and taken at once, kicking and banning ask first with a
// reason the player reads (AdminActionDialog, modal).
SettingsPage {
    id: page

    required property AdminController admin
    // The screen's modal dialog for actions that ask first.
    required property AdminActionDialog dialog

    function details(player: var): string {
        const parts = [player.ping < 0 ? qsTr("Ping unknown") : qsTr("Ping %1 ms").arg(player.ping), player.modeName];
        if (player.local)
            parts.push(qsTr("hosts the world"));
        else if (player.address.length > 0)
            parts.push(player.address);
        return parts.join(" · ");
    }

    title: qsTr("Players")

    SettingGroup {
        title: qsTr("Playing now (%n)", "", page.admin.players.length)

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

                title: modelData.operator ? qsTr("%1 · operator").arg(modelData.name) : modelData.name
                subtitle: page.details(modelData)
                actions: RowLayout {
                    spacing: Ui.space.xs

                    GameButton {
                        objectName: "adminOp" + row.index
                        text: row.modelData.operator ? qsTr("Take rights") : qsTr("Make operator")
                        // The host keeps his rights (the local player is always an operator).
                        enabled: !row.modelData.local
                        onClicked: row.modelData.operator ? page.dialog.takeRights(page.admin, row.modelData.name) : page.admin.op(row.modelData.name)
                    }
                    GameButton {
                        objectName: "adminKick" + row.index
                        kind: GameButton.Destructive
                        text: qsTr("Kick …")
                        enabled: !row.modelData.local
                        onClicked: page.dialog.ask(qsTr("Kick %1").arg(row.modelData.name),
                                                qsTr("%1 leaves the server now and may join again.").arg(row.modelData.name),
                                                qsTr("Kick"), true, reason => page.admin.kick(row.modelData.name, reason))
                    }
                    GameButton {
                        objectName: "adminBan" + row.index
                        kind: GameButton.Destructive
                        text: qsTr("Ban …")
                        enabled: !row.modelData.local
                        onClicked: page.dialog.ask(qsTr("Ban %1").arg(row.modelData.name),
                                                qsTr("%1 leaves the server now and may not join again until unbanned on the Lists page.").arg(row.modelData.name),
                                                qsTr("Ban"), true, reason => page.admin.ban(row.modelData.name, reason))
                    }
                }
            }
        }
    }
}
