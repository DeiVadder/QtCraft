// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// A character preview (CharacterPreview) that exists only while it is shown and a few seconds
// after: a hidden preview gives its GPU memory back (its colour and multisample buffers, and the
// shared texture atlas with the last preview; decision E-231). Until the figure is drawn (the menu
// builds the atlas in the background) a busy indicator stands in for it.
Item {
    id: host

    // GameController.preview.
    required property var source
    property string previewName
    property int framesPerSecond: 30
    // Covered by another screen: the picture stays as it is.
    property bool paused: false
    property bool interactive: true
    // The CharacterPreview while it exists, else null.
    readonly property CharacterPreview preview: loader.item as CharacterPreview

    // Kept this long after hiding, so a screen opened again at once shows it without delay.
    readonly property int keepMs: 3000
    property bool live: visible

    onVisibleChanged: {
        if (visible) {
            keepTimer.stop();
            live = true;
        } else {
            keepTimer.restart();
        }
    }

    Timer {
        id: keepTimer

        interval: host.keepMs
        onTriggered: host.live = false
    }

    Loader {
        id: loader

        anchors.fill: parent
        active: host.live
        sourceComponent: CharacterPreview {
            objectName: host.previewName
            source: host.source
            // No more often than the window may draw (its frame rate limit, decision E-835).
            framesPerSecond: host.source.frameLimit > 0 ? Math.min(host.framesPerSecond, host.source.frameLimit) : host.framesPerSecond
            paused: host.paused
            interactive: host.interactive
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        // Not larger than a small preview (the main menu's profile).
        width: Math.min(parent.width, implicitWidth)
        height: width
        running: host.visible && !(host.preview && host.preview.hasFigure)
        visible: running
    }
}
