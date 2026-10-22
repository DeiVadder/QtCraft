import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One action on the gamepad page (decision E-614): its name, the button it is on (a fixed
// combination, a modifier held plus a button, is shown read-only, decision E-692) and "Change",
// which waits for the next button while `capturing`.
RowLayout {
    id: bindingRow

    required property GameController game
    required property var modelData
    property bool capturing
    property bool changeEnabled

    signal changeClicked

    function glyph(button: string): var {
        return game.gamepad.pads.glyph(game.gamepad.pads.family, button);
    }

    Layout.fillWidth: true
    spacing: Ui.space.s

    Label {
        Layout.fillWidth: true
        text: bindingRow.modelData.label
        font.pixelSize: Ui.type.body
        color: Theme.text
        elide: Text.ElideRight
    }
    PadGlyph {
        visible: bindingRow.modelData.button.length > 0
        symbol: bindingRow.glyph(bindingRow.modelData.button)
    }
    PadGlyph {
        visible: bindingRow.modelData.comboModifier !== undefined
        symbol: bindingRow.glyph(bindingRow.modelData.comboModifier ?? "")
    }
    Label {
        visible: bindingRow.modelData.comboModifier !== undefined
        text: "+"
        color: Theme.text
    }
    PadGlyph {
        visible: bindingRow.modelData.comboButton !== undefined
        symbol: bindingRow.glyph(bindingRow.modelData.comboButton ?? "")
    }
    Label {
        visible: bindingRow.modelData.button.length === 0 && bindingRow.modelData.comboModifier === undefined
        text: qsTr("none")
        color: Theme.textMuted
    }
    GameButton {
        Layout.preferredWidth: Ui.size.padButton
        enabled: bindingRow.changeEnabled
        kind: bindingRow.capturing ? GameButton.Primary : GameButton.Secondary
        text: bindingRow.capturing ? qsTr("Press a button …") : qsTr("Change")
        Accessible.name: qsTr("Change the button of %1").arg(bindingRow.modelData.label)
        onClicked: bindingRow.changeClicked()
    }
}
