// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A reference to another handbook entry: icon, name and an optional note; a click opens the entry.
Rectangle {
    id: chip

    // {target, text, icon, note}
    required property var link

    signal activated(string target)

    implicitWidth: row.implicitWidth + 2 * Ui.space.s
    implicitHeight: Ui.size.handbookLink
    activeFocusOnTab: true
    Accessible.role: Accessible.Link
    Accessible.name: chip.link.note.length > 0 ? qsTr("%1 (%2)").arg(chip.link.text).arg(chip.link.note) : chip.link.text
    radius: Ui.radius
    color: hover.hovered ? Theme.linkHover : Theme.linkBackground
    border.color: Theme.separator

    RowLayout {
        id: row

        x: Ui.space.s
        anchors.verticalCenter: parent.verticalCenter
        spacing: Ui.space.xs

        Image {
            Layout.preferredWidth: Ui.size.icon
            Layout.preferredHeight: Ui.size.icon
            visible: chip.link.icon.length > 0
            source: visible ? ItemIcons.source(chip.link.icon, ItemIcons.generation) : ""
            sourceSize: Qt.size(Ui.size.icon, Ui.size.icon)
            smooth: false
        }

        Label {
            text: chip.link.text
            color: Theme.link
            font.pixelSize: Ui.type.body
        }

        Label {
            visible: chip.link.note.length > 0
            text: "(" + chip.link.note + ")"
            color: Theme.textMuted
            font.pixelSize: Ui.type.body
        }
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: chip.activated(chip.link.target)
    }
}
