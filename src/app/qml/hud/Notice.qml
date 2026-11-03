// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// A short notice at the top, shown for shownMs; show() starts it again with its text. The photo
// mode's notices (a screenshot saved or failed, the HUD hidden; decision E-692) and adventure
// mode's refusal of an action (decision E-735) use it (Notices.qml).
Rectangle {
    id: notice

    property int shownMs: 2500
    // Read by the column of notices in Notices.qml (the effective `visible` would loop through it).
    readonly property bool showing: timer.running
    property string message
    // The label's objectName, which tells the notices apart for tests and the control interface.
    property string labelName

    // Shows `text` for shownMs.
    function show(text: string) {
        message = text;
        timer.restart();
    }

    // A longer text wraps (the graphics notice, decision E-963).
    readonly property real maxTextWidth: Ui.hudPx(560)

    width: label.width + Ui.hudPx(28)
    height: label.implicitHeight + Ui.hudPx(14)
    radius: Ui.hudPx(4)
    color: Theme.hudBackdrop
    visible: timer.running

    Label {
        id: label

        objectName: notice.labelName
        anchors.centerIn: parent
        width: Math.min(implicitWidth, notice.maxTextWidth)
        wrapMode: Text.WordWrap
        horizontalAlignment: Text.AlignHCenter
        text: notice.message
        color: Theme.text
        font.pixelSize: Ui.hudPx(15)
    }

    Timer {
        id: timer

        interval: notice.shownMs
    }
}
