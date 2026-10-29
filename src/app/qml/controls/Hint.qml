// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A note about a group or a page (decision E-845, docs/UI-Leitfaden.md 6.8): a box with a bar on
// the left in the colour of its kind. Warnings and errors carry a symbol as well (!, ×), so colour
// is never the only sign. The text is plain (it may hold paths and names from files).
Rectangle {
    id: hint

    enum Kind {
        Info,
        Warning,
        Error
    }

    property int kind: Hint.Info
    property alias text: label.text
    readonly property color barColor: kind === Hint.Error ? Theme.error : kind === Hint.Warning ? Theme.warning : Theme.link

    implicitWidth: row.implicitWidth + 2 * Ui.space.m
    implicitHeight: row.implicitHeight + 2 * Ui.space.s
    radius: Ui.radius
    color: Theme.linkBackground
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Rectangle {
        width: Ui.size.bar
        height: parent.height
        color: hint.barColor
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.leftMargin: Ui.space.m
        anchors.rightMargin: Ui.space.m
        anchors.topMargin: Ui.space.s
        anchors.bottomMargin: Ui.space.s
        spacing: Ui.space.s

        Label {
            Layout.alignment: Qt.AlignTop
            visible: hint.kind !== Hint.Info
            text: hint.kind === Hint.Error ? "×" : "!"
            font.bold: true
            font.pixelSize: Ui.type.body
            color: hint.barColor
        }
        Label {
            id: label

            Layout.fillWidth: true
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.pixelSize: Ui.type.body
            color: Theme.text
        }
    }
}
