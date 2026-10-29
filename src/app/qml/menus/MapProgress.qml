// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// The thin bar under the map preview while the map is drawn (decision E-930, NW-8 of
// docs/Menue-Pruefung.md): the share of the running build's passes that have arrived
// (WorldMapPreview.progress: coarse, fine and, with villages shown, the villages and structures);
// until the first of them arrives a short piece runs along the bar, also while an older map is
// still shown. Hidden once the map is done. Keeps its height while hidden, so the legend does not
// jump.
Item {
    id: bar

    required property WorldMapPreview preview
    readonly property bool waiting: preview.busy && preview.progress <= 0

    objectName: "worldMapProgress"
    implicitHeight: Ui.size.track
    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Drawing the map")
    Accessible.description: qsTr("%1 % drawn").arg(Math.round(preview.progress * 100))
    Accessible.ignored: !preview.busy

    Rectangle {
        anchors.fill: parent
        visible: bar.preview.busy
        radius: Ui.radius
        color: Theme.track
        clip: true

        // The passes already arrived.
        Rectangle {
            visible: !bar.waiting
            width: parent.width * bar.preview.progress
            height: parent.height
            radius: Ui.radius
            color: Theme.accent
        }

        // A piece running along the bar while no pass of the running build has arrived.
        Rectangle {
            id: runner

            visible: bar.waiting
            width: parent.width / 4
            height: parent.height
            radius: Ui.radius
            color: Theme.accent

            NumberAnimation on x {
                running: bar.waiting
                loops: Animation.Infinite
                from: -runner.width
                to: runner.parent.width
                duration: 900
            }
        }
    }
}
