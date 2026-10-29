// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

// Experience levels as the workbench screens show them (decision E-884): a spark and the number (or
// a short text) in the pixel font; green when the player has the levels, red when they lack them
// (`missing`), grey without the spark when nothing can be had. On the panel the text is outlined;
// on parchment (`onParchment`) it takes the parchment's own tones instead: dark green on the light
// parchment of a usable offer, light red on the dark parchment of an unusable one.
Row {
    id: badge

    property string text
    property bool available: true
    property bool missing: false
    property bool onParchment: false
    // The palette role of the tone, for the spark and the number.
    readonly property string toneName: {
        if (missing) {
            return onParchment ? "errorOnParchment" : "error";
        }
        if (available) {
            return onParchment ? "levelOnParchment" : "level";
        }
        return "textMuted";
    }
    readonly property color tone: {
        if (missing) {
            return onParchment ? Theme.errorOnParchment : Theme.error;
        }
        return available ? (onParchment ? Theme.levelOnParchment : Theme.level) : Theme.textMuted;
    }

    spacing: Ui.space.xs

    PixelArt {
        anchors.verticalCenter: parent.verticalCenter
        visible: badge.available || badge.missing
        picture: "spark/" + badge.toneName
    }

    Label {
        anchors.verticalCenter: parent.verticalCenter
        text: badge.text
        color: badge.tone
        style: badge.onParchment ? Text.Normal : Text.Outline
        styleColor: Theme.textOutline
        font.family: Ui.pixelFont.name
        font.pixelSize: Ui.type.section
        Accessible.ignored: true
    }
}
