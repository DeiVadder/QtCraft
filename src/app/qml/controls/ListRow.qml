import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A row of a list of things (decision E-845, docs/UI-Leitfaden.md 6.9): worlds, servers, mods,
// texture packs. A picture on the left (optional), the title in bold and a subtitle, further lines
// below them, and the row's actions on the right. Texts are plain (names and descriptions come from
// files and the network). An interactive row answers clicks and double clicks as a whole; a
// focusable one also takes the focus and is clicked with Enter, Space and the pad's A.
Rectangle {
    id: row

    property alias title: titleLabel.text
    property alias subtitle: subtitleLabel.text
    // The picture on the left, the further lines under the subtitle, the actions on the right.
    property alias leading: leadingSlot.data
    default property alias details: detailColumn.data
    property alias actions: actionColumn.data
    // The row answers the mouse as a whole: lit under it, clicked() and doubleClicked().
    property bool interactive: false
    // The row itself takes the focus (Tab, the pad): Enter, Space and the pad's A click it.
    property bool focusable: false

    signal clicked
    signal doubleClicked

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight + 2 * Ui.space.s
    radius: Ui.radius
    color: interactive && hover.hovered ? Theme.rowHighlight : Theme.panelRaised
    activeFocusOnTab: focusable
    Keys.onReturnPressed: event => event.accepted = row.focusable && row.click()
    Keys.onEnterPressed: event => event.accepted = row.focusable && row.click()
    Keys.onSpacePressed: event => event.accepted = row.focusable && row.click()

    // Clicks the row from the keyboard; true (the key is taken).
    function click(): bool {
        clicked();
        return true;
    }

    FocusOutline {
        visible: row.focusable && row.activeFocus
    }

    HoverHandler {
        id: hover

        enabled: row.interactive
    }
    TapHandler {
        enabled: row.interactive
        onTapped: row.clicked()
        onDoubleTapped: row.doubleClicked()
    }

    RowLayout {
        id: layout

        x: Ui.space.m
        y: Ui.space.s
        width: parent.width - 2 * Ui.space.m
        spacing: Ui.space.m

        Item {
            id: leadingSlot

            Layout.alignment: Qt.AlignTop
            visible: children.length > 0
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }

        // Takes the width the picture and the actions leave (wrapped texts do not widen it).
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.alignment: Qt.AlignTop
            spacing: Ui.space.xs

            Label {
                id: titleLabel

                Layout.fillWidth: true
                textFormat: Text.PlainText
                font.pixelSize: Ui.type.body
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight
            }
            Label {
                id: subtitleLabel

                Layout.fillWidth: true
                visible: text.length > 0
                textFormat: Text.PlainText
                font.pixelSize: Ui.type.help
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
            ColumnLayout {
                id: detailColumn

                Layout.fillWidth: true
                spacing: Ui.space.xs
            }
        }

        ColumnLayout {
            id: actionColumn

            // A layout fills its width by default: without actions it would halve the texts' room.
            Layout.fillWidth: false
            Layout.alignment: Qt.AlignTop
            visible: children.length > 0
            spacing: Ui.space.xs
        }
    }
}
