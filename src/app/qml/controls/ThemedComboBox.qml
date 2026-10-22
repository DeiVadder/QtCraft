pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtCraft

// The game's drop-down list (decision E-845, docs/UI-Leitfaden.md 6.7): a sunken field with the
// choice and an arrow, and a popup on the panel whose highlighted row is the accent with its dark
// text. The Basic style would draw that row in the palette's Light with HighlightedText, which the
// palette cannot keep readable on both Light and the accent (QcUiPalette::controlPalette). Every
// drop-down of the menus is one of these; SettingDropdown puts it into a setting row.
ComboBox {
    id: box

    implicitHeight: Ui.size.field
    font.pixelSize: Ui.type.body

    background: Rectangle {
        implicitWidth: Ui.size.value
        color: Theme.field

        Bevel {
            sunken: true
        }
        FocusOutline {
            visible: box.visualFocus
        }
    }
    contentItem: Label {
        leftPadding: Ui.space.s
        rightPadding: Ui.space.s
        text: box.displayText
        font: box.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Label {
        x: box.width - width - Ui.space.s
        y: (box.height - height) / 2
        text: "▾"
        font.pixelSize: Ui.type.body
        color: Theme.textMuted
    }

    delegate: ItemDelegate {
        id: choice

        required property var model
        required property var modelData
        required property int index

        width: ListView.view.width
        implicitHeight: Ui.size.field
        leftPadding: Ui.space.s
        rightPadding: Ui.space.s
        topPadding: 0
        bottomPadding: 0
        text: box.textRole ? model[box.textRole] : modelData
        highlighted: box.highlightedIndex === index
        hoverEnabled: box.hoverEnabled

        background: Rectangle {
            color: choice.highlighted ? Theme.accent : Theme.clear
        }
        contentItem: Label {
            text: choice.text
            font.pixelSize: Ui.type.body
            font.bold: box.currentIndex === choice.index
            color: choice.highlighted ? Theme.accentText : Theme.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    popup: Popup {
        y: box.height
        width: box.width
        // A popup taller than the window shrinks to it within the margins.
        height: contentItem.implicitHeight + topPadding + bottomPadding
        topMargin: Ui.space.s
        bottomMargin: Ui.space.s
        padding: Ui.size.line

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: box.delegateModel
            currentIndex: box.highlightedIndex
            highlightMoveDuration: 0
            ScrollBar.vertical: ThemedScrollBar {}
        }
        background: Rectangle {
            color: Theme.field
            border.color: Theme.panelBorder
            border.width: Ui.size.line
        }
    }
}
