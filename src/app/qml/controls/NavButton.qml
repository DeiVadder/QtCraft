import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One entry of a side bar (decision E-845, docs/UI-Leitfaden.md 6.11): the settings' categories
// (NavList) and the main menu's entries. The chosen one (`checked`) on the raised surface with an
// accent bar on the left; a key can stand as a plate on the right (keyHint). Enter clicks it as Space
// does.
AbstractButton {
    id: entry

    // The key shown as a plate at the right ("H"); empty for none.
    property string keyHint: ""
    // A short muted text at the right (a count); empty for none.
    property string note: ""
    // A category of pages (read as a tab) rather than an action.
    property bool page: false

    Layout.fillWidth: true
    // Kept off the window's edge, so the focus outline and the pad's ring stay inside it.
    Layout.leftMargin: Ui.space.xs
    Layout.rightMargin: Ui.space.xs
    // Disabled (the main menu's side bar during a join) it dims.
    opacity: enabled ? 1 : Ui.disabledOpacity
    implicitHeight: Ui.size.navItem
    leftPadding: Ui.space.m
    rightPadding: Ui.space.s
    focusPolicy: Qt.StrongFocus
    // The focus outline reaches over the neighbours.
    z: visualFocus ? 1 : 0
    Accessible.role: page ? Accessible.PageTab : Accessible.Button
    Accessible.name: text
    Accessible.selected: checked
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: Rectangle {
        color: entry.checked ? Theme.panelRaised : entry.hovered ? Theme.rowHighlight : Theme.clear

        Rectangle {
            visible: entry.checked
            width: Ui.size.bar
            height: parent.height
            color: Theme.accent
        }
        FocusOutline {
            visible: entry.visualFocus
        }
    }
    contentItem: RowLayout {
        spacing: Ui.space.s

        Label {
            Layout.fillWidth: true
            text: entry.text
            font.pixelSize: Ui.type.body
            font.bold: entry.checked
            color: entry.checked ? Theme.text : Theme.textSecondary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Label {
            visible: entry.note.length > 0
            text: entry.note
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
            Accessible.ignored: true
        }
        KeyBadge {
            visible: entry.keyHint.length > 0
            text: entry.keyHint
        }
    }
}
