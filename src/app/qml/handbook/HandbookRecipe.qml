pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A recipe drawn like the crafting screen: the grid (or a machine's input), an arrow and the result;
// below it how and where it is made. Every cell opens its entry when clicked.
ColumnLayout {
    id: view

    // See QcHandbookJson::entry: {kind, width, cells, result, count, note, machines, ...}
    required property var recipe

    signal activated(string target)

    spacing: Ui.px(4)

    component Cell: Rectangle {
        id: cell

        property var link: null
        property int count: 1

        implicitWidth: Ui.px(44)
        implicitHeight: Ui.px(44)
        // Cells with a link take the pad's focus (decision E-583).
        activeFocusOnTab: cell.link !== null
        Accessible.role: Accessible.Link
        Accessible.name: !cell.link ? qsTr("Empty") : (cell.count > 1 ? qsTr("%1 × %2").arg(cell.count).arg(cell.link.text) : cell.link.text)
        color: hover.hovered && cell.link ? Theme.slotHover : Theme.panelRaised
        border.color: Theme.slotBorder
        radius: Ui.px(2)

        Image {
            anchors.fill: parent
            anchors.margins: Ui.px(6)
            visible: cell.link !== null && cell.link.icon.length > 0
            source: visible ? ItemIcons.source(cell.link.icon, ItemIcons.generation) : ""
            sourceSize: Qt.size(32, 32)
            smooth: false
        }

        // Ingredients without an icon (a group of no items) show their name.
        Label {
            anchors.fill: parent
            anchors.margins: Ui.px(2)
            visible: cell.link !== null && cell.link.icon.length === 0
            text: cell.link ? cell.link.text : ""
            color: Theme.text
            font.pixelSize: Ui.px(9)
            wrapMode: Text.Wrap
            elide: Text.ElideRight
        }

        Label {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Ui.px(3)
            visible: cell.count > 1
            text: cell.count
            color: Theme.text
            style: Text.Outline
            styleColor: Theme.textOutline
            font.bold: true
        }

        ThemedToolTip {
            shown: hover.hovered && cell.link !== null
            text: cell.link ? cell.link.text : ""
        }

        HoverHandler {
            id: hover

            cursorShape: cell.link ? Qt.PointingHandCursor : Qt.ArrowCursor
        }

        TapHandler {
            enabled: cell.link !== null
            onTapped: view.activated(cell.link.target)
        }
    }

    RowLayout {
        spacing: Ui.px(8)

        Grid {
            columns: view.recipe.width
            spacing: Ui.px(2)

            Repeater {
                model: view.recipe.cells

                Cell {
                    required property var modelData

                    link: modelData
                }
            }
        }

        Label {
            text: view.recipe.kind === "machine" ? "⟶" : "→"
            color: view.recipe.kind === "machine" ? Theme.warning : Theme.text
            font.pixelSize: Ui.px(26)
        }

        Cell {
            link: view.recipe.result
            count: view.recipe.count
        }
    }

    Label {
        Layout.maximumWidth: Ui.px(360)
        text: view.recipe.note
        color: Theme.textMuted
        font.pixelSize: Ui.px(12)
        wrapMode: Text.Wrap
    }
}
