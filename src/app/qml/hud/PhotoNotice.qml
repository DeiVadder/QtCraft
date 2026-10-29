// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// A short notice at the top after the screenshot key saved a picture or failed to (decision E-692),
// also when adventure mode refuses an action (decision E-735), shown for shownMs; show() starts it
// again. It is hidden while the picture is taken.
Rectangle {
    id: notice

    readonly property int shownMs: 2500
    // Read by the column of notices in Main.qml (the effective `visible` would loop through it).
    readonly property bool showing: timer.running
    property string message
    // The label's objectName, which tests and the control interface look for.
    property string labelName: "photoNotice"

    // Shows `text` for shownMs.
    function show(text: string) {
        message = text;
        timer.restart();
    }

    width: label.implicitWidth + Ui.hudPx(28)
    height: label.implicitHeight + Ui.hudPx(14)
    radius: Ui.hudPx(4)
    color: Theme.hudBackdrop
    visible: timer.running

    Label {
        id: label

        objectName: notice.labelName
        anchors.centerIn: parent
        text: notice.message
        color: Theme.text
        font.pixelSize: Ui.hudPx(15)
    }

    Timer {
        id: timer

        interval: notice.shownMs
    }
}
