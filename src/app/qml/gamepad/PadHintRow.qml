pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// A row of pad buttons with what they do (decision E-612): each entry {input, text} shows the
// button's symbol for the family of the pad in use (PadGlyph) and its text; an entry {inputs, text}
// shows several buttons that share the text ("LB RB category"); an optional `name` becomes the
// text's objectName (for tests). Inputs are named as in
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

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Ui.px(3)

                Repeater {
                    model: entry.modelData.inputs ?? [entry.modelData.input]

                    PadGlyph {
                        required property string modelData

                        size: Math.round(row.fontSize * 20 / 13)
                        symbol: row.game.gamepad.pads.glyph(row.game.gamepad.pads.family, modelData)
                    }
                }
            }
            Label {
                objectName: entry.modelData.name ?? ""
                anchors.verticalCenter: parent.verticalCenter
                text: entry.modelData.text
                color: Theme.text
                font.pixelSize: row.fontSize
            }
        }
    }
}
