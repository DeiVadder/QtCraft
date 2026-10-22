pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The category list on the left of a screen with several pages (decision E-845,
// docs/UI-Leitfaden.md 6.11): the chosen category on the raised surface with an accent bar on the
// left. A click chooses one; Ctrl+Tab (its shortcuts) and the pad's shoulders (takeCommand(), from
// the screen's gamepadCommand()) switch it.
ColumnLayout {
    id: nav

    // The categories' names, in order.
    property list<string> model
    property int currentIndex

    // The category after (step 1) or before (step -1) the current one, wrapping around.
    function next(step: int) {
        if (model.length > 0)
            currentIndex = (currentIndex + step + model.length) % model.length;
    }

    // The pad's shoulders (gamepadCommand "tabNext", "tabPrevious") switch the category: true if
    // `command` was one of them.
    function takeCommand(command: string): bool {
        if (command !== "tabNext" && command !== "tabPrevious")
            return false;

        next(command === "tabNext" ? 1 : -1);
        return true;
    }

    spacing: 0

    // Ctrl+Tab and Ctrl+Shift+Tab switch the category while the list is shown, wherever the focus
    // is on its screen: shortcuts come before the keys, and a control with the focus would take a
    // Tab with Meta to move the focus. On a Mac Qt names the Control key Meta (and Cmd Ctrl;
    // Cmd+Tab belongs to the system), so both count.
    Shortcut {
        sequences: ["Ctrl+Tab", "Meta+Tab"]
        enabled: nav.visible
        onActivated: nav.next(1)
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Tab", "Meta+Shift+Tab"]
        enabled: nav.visible
        onActivated: nav.next(-1)
    }

    Repeater {
        model: nav.model

        delegate: AbstractButton {
            id: category

            required property string modelData
            required property int index

            Layout.fillWidth: true
            implicitHeight: Ui.size.navItem
            checked: index === nav.currentIndex
            text: modelData
            focusPolicy: Qt.StrongFocus
            // The focus outline reaches over the neighbours.
            z: visualFocus ? 1 : 0
            Accessible.role: Accessible.PageTab
            Accessible.name: text
            Accessible.selected: checked
            onClicked: nav.currentIndex = index

            background: Rectangle {
                color: category.checked ? Theme.panelRaised : category.hovered ? Theme.rowHighlight : Theme.clear

                Rectangle {
                    visible: category.checked
                    width: Ui.size.bar
                    height: parent.height
                    color: Theme.accent
                }
                FocusOutline {
                    visible: category.visualFocus
                }
            }
            contentItem: Label {
                leftPadding: Ui.space.l
                rightPadding: Ui.space.s
                text: category.text
                font.pixelSize: Ui.type.body
                font.bold: category.checked
                color: category.checked ? Theme.text : Theme.textSecondary
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }
}
