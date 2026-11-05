// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// The bar of the loading screen (decision E-943): the share of the world start done, eased
// between the values it is given; while `waiting` a short piece runs along the part still to do,
// as on the map preview's bar (MapProgress).
Item {
    id: bar

    property real fraction: 0
    property bool waiting: false

    objectName: "loadingBar"
    implicitHeight: Ui.size.track
    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Loading")
    Accessible.description: qsTr("%1 % done").arg(Math.round(bar.fraction * 100))

    Rectangle {
        anchors.fill: parent
        radius: Ui.radius
        color: Theme.track
        clip: true

        // A piece running over the part still to do.
        Rectangle {
            id: runner

            readonly property real start: fill.width

            visible: bar.waiting && bar.visible
            width: parent.width / 5
            height: parent.height
            radius: Ui.radius
            color: Theme.accent
            opacity: 0.5

            NumberAnimation on x {
                running: runner.visible
                loops: Animation.Infinite
                from: runner.start - runner.width
                to: runner.parent.width
                duration: 1100
            }
        }

        Rectangle {
            id: fill

            width: parent.width * Math.max(0, Math.min(1, bar.fraction))
            height: parent.height
            radius: Ui.radius
            color: Theme.accent

            Behavior on width {
                NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
            }
        }
    }
}
