pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// A row of pad buttons with what they do (decision E-612): each entry {input, text} shows the
// button's symbol for the family of the pad in use (PadGlyph) and its text. Inputs are named as in
// GameController.gamepad.pads.glyph ("south", "dpadLeft", "rightTrigger", "rightStickMove").
Row {
    id: row

    required property GameController game
    property var entries: []
    // Already scaled (Ui.px or Ui.hudPx, decision E-725); the symbols grow with it.
    property int fontSize: Ui.px(13)

    spacing: Ui.px(14)

    Repeater {
        model: row.entries

        Row {
            id: entry

            required property var modelData

            spacing: Ui.px(5)

            PadGlyph {
                anchors.verticalCenter: parent.verticalCenter
                size: Math.round(row.fontSize * 20 / 13)
                symbol: row.game.gamepad.pads.glyph(row.game.gamepad.pads.family, entry.modelData.input)
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: entry.modelData.text
                color: Theme.text
                font.pixelSize: row.fontSize
            }
        }
    }
}
