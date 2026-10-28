import QtQuick
import QtQuick.Layouts
import QtCraft

// An on/off setting (decision E-845, docs/UI-Leitfaden.md 6.5): a switch, not a check box, with
// the label on the left like every row. On, its track is the accent and the knob sits right.
// `toggled()` follows a change by the player.
SettingRow {
    id: setting

    property alias checked: toggle.checked

    signal toggled

    // Switches it as a click would (also the test hook "click:<objectName>").
    function click() {
        toggle.click();
    }

    GameSwitch {
        id: toggle

        Accessible.name: setting.label
        onToggled: setting.toggled()
    }

    // Keeps the switch at the start of the control column.
    Item {
        Layout.fillWidth: true
    }
}
