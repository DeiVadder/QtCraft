// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One recently joined server in the multiplayer screen (decision E-852, a ListRow): its name and
// address and when it was joined; a click puts the address into the field, a double click and Join
// join at once, × removes the entry.
ListRow {
    id: row

    required property var modelData
    // Without a server name the address is the title already.
    readonly property string place: modelData.name.length > 0 ? modelData.address : ""

    signal chosen(string address)
    signal joinRequested(string address)
    signal removeRequested(string address)

    interactive: true
    // Tab and the pad reach the row: choosing it puts the address into the field.
    focusable: true
    title: modelData.name.length > 0 ? modelData.name : modelData.address
    subtitle: modelData.lastJoined.length === 0 ? place
              : place.length > 0 ? qsTr("%1 · last joined %2").arg(place).arg(modelData.lastJoined)
              : qsTr("last joined %1").arg(modelData.lastJoined)
    Accessible.role: Accessible.ListItem
    Accessible.name: modelData.name.length > 0 ? qsTr("%1 (%2)").arg(modelData.name).arg(modelData.address) : modelData.address

    onClicked: row.chosen(row.modelData.address)
    onDoubleClicked: row.joinRequested(row.modelData.address)

    actions: RowLayout {
        spacing: Ui.space.xs

        GameButton {
            text: qsTr("Join")
            Accessible.name: qsTr("Join %1").arg(row.modelData.address)
            onClicked: row.joinRequested(row.modelData.address)
        }

        GameButton {
            id: removeButton

            kind: GameButton.Quiet
            text: "×"
            Accessible.name: qsTr("Remove %1 from the list").arg(row.modelData.address)
            onClicked: row.removeRequested(row.modelData.address)

            ThemedToolTip {
                shown: removeButton.hovered
                text: qsTr("Remove from the list")
            }
        }
    }
}
