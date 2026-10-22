import QtQuick.Layouts

// The column of a SettingRow's controls (decision E-845): while the row dims, its controls do not
// dim themselves again (GameButton and GameSwitch ask their parent as a ControlSlot).
RowLayout {
    property bool dimsControls
}
