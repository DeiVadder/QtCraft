import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One choice of a multiple choice within a group (decision E-845, docs/UI-Leitfaden.md 6.5): a
// check box, for the debug visualisations; a single on/off setting is a SettingSwitch. A sunken box
// with an outline that stands out from the panel, in the accent with a check mark while chosen, and
// the text on its right. Space and Enter switch it; `toggled()` follows a change by the player.
CheckBox {
    id: check

    // A change by the player: toggled() follows it as after a click.
    function flip() {
        check.toggle();
        check.toggled();
    }

    Layout.fillWidth: true
    implicitHeight: Math.max(Ui.size.field, contentItem.implicitHeight)
    padding: 0
    spacing: Ui.space.s
    font.pixelSize: Ui.type.body
    Keys.onReturnPressed: flip()
    Keys.onEnterPressed: flip()

    indicator: Rectangle {
        x: check.leftPadding
        y: (check.height - height) / 2
        implicitWidth: Ui.size.icon
        implicitHeight: Ui.size.icon
        radius: Ui.radius
        color: check.checked ? Theme.accent : Theme.field
        border.color: check.checked ? Theme.accent : Theme.track
        border.width: Ui.size.line

        Label {
            anchors.centerIn: parent
            visible: check.checked
            text: "✓"
            font.pixelSize: Ui.type.body
            font.bold: true
            color: Theme.accentText
        }
        FocusOutline {
            visible: check.visualFocus
        }
    }
    contentItem: Label {
        leftPadding: check.indicator.width + check.spacing
        text: check.text
        font: check.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
