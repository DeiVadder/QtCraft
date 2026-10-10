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

    readonly property int level: overlay.game.hud.debug.level

    HudLabel {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        visible: overlay.level > 0
        text: overlay.game.hud.debug.generalText
        font.pixelSize: overlay.level === 2 ? 13 : 14
    }

    Column {
        objectName: "debugTimings"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 8
        anchors.topMargin: 40
        visible: overlay.level === 2
        spacing: 4

        HudLabel {
            anchors.right: parent.right
            text: overlay.game.hud.debug.timingsText
            font: overlay.game.hud.debug.monoFont
        }

        DebugGraphPanel {
            id: framePanel

            title: (framePanel.limitShown ? qsTr("Frame interval, 0–%1 ms, line: 60 FPS") : qsTr("Frame interval, 0–%1 ms, limit off scale"))
                   .arg(Math.round(framePanel.scaleTop))
            values: overlay.game.hud.debug.frameGraph
            maxValue: 33.3
            limit: 16.7
            lineColor: Theme.graphFrame
        }

        // Ticks usually take far less than their 50 ms: the scale follows the longest one shown.
        DebugGraphPanel {
            id: tickPanel

            // The 50 ms budget is only drawn once the scale reaches it (short titles: the panel is narrow).
            title: (tickPanel.limitShown ? qsTr("Server tick, 0–%1 ms, line: 50 ms") : qsTr("Server tick, 0–%1 ms, limit off scale"))
                   .arg(tickPanel.scaleTop.toFixed(tickPanel.scaleTop < 10 ? 1 : 0))
            values: overlay.game.hud.debug.tickGraph
            maxValue: 100
            autoScale: true
            limit: 50
            lineColor: Theme.graphTick
        }
    }

    component DebugGraphPanel: Rectangle {
        id: panel

        property string title
        property list<real> values
        property real maxValue
        // Scale to 1.5 times the highest value (at least 2 ms, at most maxValue).
        property bool autoScale
        property real limit
        property color lineColor
        readonly property real scaleTop: autoScale ? Math.min(maxValue, Math.max(2, 1.5 * Math.max(0, ...values))) : maxValue
        readonly property bool limitShown: graph.limitShown

        anchors.right: parent.right
        width: 260
        height: 64
        color: Theme.hudBackdrop
        radius: 3

        DebugGraph {
            id: graph

            anchors.fill: parent
            anchors.margins: 4
            anchors.topMargin: 18
            values: panel.values
            capacity: 240
            maxValue: panel.scaleTop
            limit: panel.limit
            color: panel.lineColor
            limitColor: Theme.graphLimit
        }

        Label {
            x: 6
            y: 2
            text: panel.title
            color: Theme.text
            font.pixelSize: 11
        }
    }
}
