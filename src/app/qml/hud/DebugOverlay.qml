// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// The debug display (decision E-225): the compact line (frame rate and frame time) or, at the full
// level, the general column on the left and the time breakdown with the frame and tick graphs on
// the right. The texts change a few times a second; the debug key cycles off, compact and full.
Item {
    id: overlay

    required property GameController game

    readonly property DebugHud debug: overlay.game.hud.debug
    readonly property int level: overlay.debug.level

    HudLabel {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Ui.hudPx(8)
        visible: overlay.level > 0
        text: overlay.debug.generalText
        font.pixelSize: Ui.hudPx(overlay.level === 2 ? 13 : 14)
    }

    Column {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Ui.hudPx(8)
        anchors.topMargin: Ui.hudPx(40)
        visible: overlay.level === 2
        spacing: Ui.hudPx(4)

        HudLabel {
            anchors.right: parent.right
            text: overlay.debug.timingsText
            font: overlay.debug.monoFont
        }

        DebugGraphPanel {
            id: framePanel

            title: (framePanel.limitShown ? qsTr("Frame interval, 0–%1 ms, line: 60 FPS") : qsTr("Frame interval, 0–%1 ms, limit off scale"))
                   .arg(Math.round(framePanel.scaleTop))
            values: overlay.debug.frameGraph
            scaleTop: overlay.debug.frameScaleMs
            limit: overlay.debug.frameBudgetMs
            lineColor: Theme.graphFrame
        }

        // Ticks usually take far less than their length: the scale follows the longest one shown
        // (DebugHud.tickScaleMs).
        DebugGraphPanel {
            id: tickPanel

            readonly property string scaleText: scaleTop.toFixed(scaleTop < 10 ? 1 : 0)

            // The tick length is only drawn once the scale reaches it (short titles: the panel is
            // narrow).
            title: tickPanel.limitShown ? qsTr("Server tick, 0–%1 ms, line: %2 ms").arg(tickPanel.scaleText).arg(overlay.debug.tickBudgetMs)
                                        : qsTr("Server tick, 0–%1 ms, limit off scale").arg(tickPanel.scaleText)
            values: overlay.debug.tickGraph
            scaleTop: overlay.debug.tickScaleMs
            limit: overlay.debug.tickBudgetMs
            lineColor: Theme.graphTick
        }
    }

    component DebugGraphPanel: Rectangle {
        id: panel

        property string title
        property list<real> values
        // The top of the scale in milliseconds.
        property real scaleTop
        property real limit
        property color lineColor
        readonly property bool limitShown: graph.limitShown

        anchors.right: parent.right
        width: Ui.hudPx(260)
        height: Ui.hudPx(64)
        color: Theme.hudBackdrop
        radius: Ui.hudPx(3)

        DebugGraph {
            id: graph

            anchors.fill: parent
            anchors.margins: Ui.hudPx(4)
            anchors.topMargin: Ui.hudPx(18)
            values: panel.values
            capacity: 240
            maxValue: panel.scaleTop
            limit: panel.limit
            color: panel.lineColor
            limitColor: Theme.graphLimit
        }

        Label {
            x: Ui.hudPx(6)
            y: Ui.hudPx(2)
            textFormat: Text.PlainText
            text: panel.title
            color: Theme.text
            font.pixelSize: Ui.hudPx(11)
        }
    }
}
