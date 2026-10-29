// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A named group of settings (decision E-845, docs/UI-Leitfaden.md 6.2): the heading in gold, the
// rows below on a raised card, with an optional help line between them for what concerns the whole
// group (how to use a table). Pages with more than five settings split them into groups, the
// common ones first.
ColumnLayout {
    id: group

    property alias title: heading.text
    property alias help: helpLabel.text
    default property alias rows: card.data

    Layout.fillWidth: true
    spacing: Ui.space.s

    Label {
        id: heading

        Layout.fillWidth: true
        font.pixelSize: Ui.type.group
        font.bold: true
        color: Theme.heading
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    Label {
        id: helpLabel

        Layout.fillWidth: true
        Layout.topMargin: -Ui.space.xs
        visible: text.length > 0
        font.pixelSize: Ui.type.help
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: card.implicitHeight + 2 * Ui.space.s
        radius: Ui.radius
        color: Theme.panelRaised

        ColumnLayout {
            id: card

            x: Ui.space.l
            y: Ui.space.s
            width: parent.width - 2 * Ui.space.l
            spacing: Ui.space.xs
        }
    }
}
