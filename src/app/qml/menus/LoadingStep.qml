// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One step of the loading screen (decision E-943): a square that is filled with a tick once the
// step is done, pulses while it runs and stays empty while it waits; its text and, for the chunk
// steps, the counts at the right.
RowLayout {
    id: step

    // The step this row stands for and the one running now (LoadingProgress.Stage).
    property int stage
    property int current
    property alias text: label.text
    property string count: ""
    readonly property bool done: current > stage
    readonly property bool running: current === stage

    Layout.fillWidth: true
    spacing: Ui.space.m
    Accessible.role: Accessible.StaticText
    Accessible.name: step.done ? qsTr("%1: done").arg(label.text) : step.running ? qsTr("%1: running").arg(label.text) : label.text

    Rectangle {
        Layout.preferredWidth: Ui.size.icon
        Layout.preferredHeight: Ui.size.icon
        radius: Ui.radius
        color: step.done ? Theme.accent : Theme.clear
        border.width: Ui.size.focus
        border.color: step.done || step.running ? Theme.accent : Theme.track

        Label {
            anchors.centerIn: parent
            visible: step.done
            text: "✓"
            color: Theme.accentText
            font.pixelSize: Ui.type.help
            font.bold: true
        }

        // The running step's square fills and empties, so a step that takes a while still moves.
        Rectangle {
            anchors.centerIn: parent
            visible: step.running
            width: parent.width / 2
            height: width
            radius: Ui.radius
            color: Theme.accent

            SequentialAnimation on opacity {
                running: step.running && step.visible
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.25; duration: 500 }
                NumberAnimation { from: 0.25; to: 1; duration: 500 }
            }
        }
    }

    Label {
        id: label

        Layout.fillWidth: true
        color: step.running ? Theme.text : step.done ? Theme.textSecondary : Theme.textMuted
        font.pixelSize: Ui.type.body
        font.bold: step.running
        elide: Text.ElideRight
    }

    Label {
        // "loadingStep4Count" for the step of stage 4 (tests read the counts).
        objectName: step.objectName + "Count"
        visible: step.count !== "" && (step.running || step.done)
        text: step.count
        color: step.running ? Theme.text : Theme.textSecondary
        font.pixelSize: Ui.type.body
        font.features: { "tnum": 1 }
    }
}
