pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A recipe drawn like the crafting screen: the grid (or a machine's input), an arrow and the result;
// below it how and where it is made. Every cell opens its entry when clicked.
ColumnLayout {
    id: view

    // See QcHandbookJson::entry: {kind, width, cells, result, count, note, machines, ...}
    required property var recipe

    signal activated(string target)

    spacing: 4

    component Cell: Rectangle {
        id: cell

        property var link: null
        property int count: 1

        implicitWidth: 44
        implicitHeight: 44
        color: hover.hovered && cell.link ? "#c0606060" : "#a0303030"
        border.color: "#80000000"
        radius: 2

        Image {
            anchors.fill: parent
            anchors.margins: 6
            visible: cell.link !== null && cell.link.icon.length > 0
            source: visible ? "image://items/" + cell.link.icon : ""
            sourceSize: Qt.size(32, 32)
            smooth: false
        }

        // Ingredients without an icon (a group of no items) show their name.
        Label {
            anchors.fill: parent
            anchors.margins: 2
            visible: cell.link !== null && cell.link.icon.length === 0
            text: cell.link ? cell.link.text : ""
            color: "white"
            font.pixelSize: 9
            wrapMode: Text.Wrap
            elide: Text.ElideRight
        }

        Label {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 3
            visible: cell.count > 1
            text: cell.count
            color: "white"
            style: Text.Outline
            styleColor: "black"
            font.bold: true
        }

        ToolTip.visible: hover.hovered && cell.link !== null
        ToolTip.text: cell.link ? cell.link.text : ""

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
        spacing: 8

        Grid {
            columns: view.recipe.width
            spacing: 2

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
            color: view.recipe.kind === "machine" ? "#ffb060" : "white"
            font.pixelSize: 26
        }

        Cell {
            link: view.recipe.result
            count: view.recipe.count
        }
    }

    Label {
        Layout.maximumWidth: 360
        text: view.recipe.note
        color: "#c8ffffff"
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
}
