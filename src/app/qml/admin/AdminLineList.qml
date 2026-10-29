// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Lines of the server's chat, log or command output (AdminLineModel, decision E-863) on the sunken
// field, newest at the bottom: time, source (the sender or the log's category) and text, which
// wraps. Warnings and errors have their colour and a sign; while the view is
// at the end (or `follow` is set) it stays there as lines arrive.
Rectangle {
    id: box

    required property AdminLineModel lines
    // Keeps the newest line in view; otherwise only while the view is at the end.
    property bool follow: true
    // What the lines are, for screen readers ("Chat", "Log").
    property string title
    // Shown while there are no lines.
    property string emptyText
    property alias view: list

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumHeight: Ui.size.lines
    radius: Ui.radius
    color: Theme.field

    Bevel {
        sunken: true
    }

    ListView {
        id: list

        // Whether the newest line is in view (kept while lines arrive).
        property bool atEnd: true

        anchors.fill: parent
        anchors.margins: Ui.space.s
        clip: true
        Accessible.role: Accessible.List
        Accessible.name: box.title
        model: box.lines
        spacing: Ui.space.xs
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ThemedScrollBar {}
        onMovementEnded: atEnd = list.atYEnd
        onCountChanged: {
            if (box.follow || atEnd)
                Qt.callLater(list.positionViewAtEnd);
        }

        delegate: Text {
            id: line

            // The roles of AdminLineModel: time, level, source, text.
            required property var model
            readonly property int level: model.level
            readonly property bool error: level >= AdminController.LogLevel.Critical
            readonly property bool warning: level === AdminController.LogLevel.Warning
            // Never only the colour: warnings and errors have a sign.
            readonly property string sign: error ? "× " : warning ? "! " : ""

            width: ListView.view.width - Ui.space.l
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            font.pixelSize: Ui.type.help
            color: error ? Theme.error : warning ? Theme.warning : level === AdminController.LogLevel.Debug ? Theme.textMuted : Theme.text
            text: sign + (model.source.length > 0 ? qsTr("%1  %2: %3").arg(model.time).arg(model.source).arg(model.text)
                                                  : qsTr("%1  %2").arg(model.time).arg(model.text))
        }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 2 * Ui.space.l
        visible: list.count === 0
        text: box.emptyText
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.pixelSize: Ui.type.body
        color: Theme.textMuted
    }
}
