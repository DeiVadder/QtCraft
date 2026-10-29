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

    implicitWidth: row.implicitWidth + Ui.px(12)
    implicitHeight: Ui.px(28)
    activeFocusOnTab: true
    Accessible.role: Accessible.Link
    Accessible.name: chip.link.note.length > 0 ? qsTr("%1 (%2)").arg(chip.link.text).arg(chip.link.note) : chip.link.text
    radius: Ui.px(3)
    color: hover.hovered ? Theme.linkHover : Theme.linkBackground
    border.color: Theme.separator

    RowLayout {
        id: row

        x: Ui.px(6)
        anchors.verticalCenter: parent.verticalCenter
        spacing: Ui.px(5)

        Image {
            Layout.preferredWidth: Ui.px(20)
            Layout.preferredHeight: Ui.px(20)
            visible: chip.link.icon.length > 0
            source: visible ? ItemIcons.source(chip.link.icon, ItemIcons.generation) : ""
            sourceSize: Qt.size(20, 20)
            smooth: false
        }

        Label {
            text: chip.link.text
            color: Theme.link
        }

        Label {
            visible: chip.link.note.length > 0
            text: "(" + chip.link.note + ")"
            color: Theme.textMuted
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
