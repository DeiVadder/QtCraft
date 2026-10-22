import QtQuick
import QtQuick.Controls
import QtCraft

// The game's text field (decision E-845): a sunken field like the drop-down lists, the text in the
// body size, a muted placeholder, and the gold outline while it has the focus. Every text field of
// the menus is one of these.
TextField {
    id: field

    implicitHeight: Ui.size.field
    leftPadding: Ui.space.s
    rightPadding: Ui.space.s
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: Ui.type.body
    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectByMouse: true

    background: Rectangle {
        implicitWidth: Ui.size.value
        color: Theme.field
        opacity: field.enabled ? 1 : Ui.disabledOpacity

        Bevel {
            sunken: true
        }
        FocusOutline {
            visible: field.activeFocus
        }
    }
}
