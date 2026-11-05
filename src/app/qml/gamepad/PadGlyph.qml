// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCraft

// One pad button as a symbol (decision E-612): a disc with a letter or word (Xbox and Nintendo
// letters, Back and Start), the four PlayStation shapes drawn as lines, a tab for shoulders and
// triggers, a stick, the d-pad cross with one arm lit, or for pads of unknown make a diamond of four
// dots with one lit. Plain shapes and letters, no logos. The glyph comes from
// GameController.gamepad.pads.glyph(family, input) ({shape, label, colour, direction}); the shapes
// are drawn by PadGlyphPainter (C++), once per change, the letters lie on it. Exists only while
// the pad leads (in the pad's loaders).
Item {
    id: glyph

    required property var symbol
    readonly property string label: symbol.label ?? ""
    property real size: Ui.px(22)

    implicitWidth: painter.wide ? Math.max(size * 1.5, glyphText.implicitWidth + Ui.px(10)) : size
    implicitHeight: size

    PadGlyphPainter {
        id: painter

        anchors.fill: parent
        symbol: glyph.symbol
    }

    Text {
        id: glyphText

        anchors.centerIn: parent
        anchors.verticalCenterOffset: painter.shape === PadGlyphPainter.Shape.Trigger ? 1 : 0
        visible: glyph.label.length > 0
        text: glyph.label
        color: painter.ink
        // Inside the stick's ring and in words the letters are smaller.
        font.pixelSize: glyph.label.length > 2 || painter.shape === PadGlyphPainter.Shape.Stick ? glyph.size * 0.4 : glyph.size * 0.55
        font.bold: true
    }
}
