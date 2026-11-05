// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Facts of a handbook page as a two-column table (HandbookPage): the label on the left, the value
// and the links on the right.
Rectangle {
    id: table

    // The facts: [{label, value, links}] (QcHandbookJson::entry).
    property var facts: []

    // A link asks for the page `target`.
    signal linkActivated(string target)

    visible: facts.length > 0
    implicitHeight: factColumn.implicitHeight + 2 * Ui.space.s
    color: Theme.rowHighlight
    radius: Ui.radius

    ColumnLayout {
        id: factColumn

        x: Ui.space.s
        y: Ui.space.s
        width: parent.width - 2 * Ui.space.s
        spacing: Ui.space.xs

        Repeater {
            model: table.facts

            RowLayout {
                id: factRow

                required property var modelData

                Layout.fillWidth: true
                spacing: Ui.space.m

                Label {
                    Layout.preferredWidth: Ui.size.handbookFactLabel
                    Layout.alignment: Qt.AlignTop
                    topPadding: Ui.space.xs
                    text: factRow.modelData.label
                    color: Theme.textMuted
                    font.pixelSize: Ui.type.body
                    wrapMode: Text.Wrap
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Ui.space.s

                    Label {
                        visible: text.length > 0
                        topPadding: Ui.space.xs
                        width: Math.min(implicitWidth, parent.width)
                        text: factRow.modelData.value
                        color: Theme.text
                        font.pixelSize: Ui.type.body
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: factRow.modelData.links

                        HandbookLink {
                            required property var modelData

                            link: modelData
                            onActivated: target => table.linkActivated(target)
                        }
                    }
                }
            }
        }
    }
}
