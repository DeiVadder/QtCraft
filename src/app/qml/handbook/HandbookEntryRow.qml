// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One row of the handbook's list of entries (HandbookScreen): an entry with its icon (and its
// category among search results), or a heading that groups the entries below it.
ItemDelegate {
    id: entryRow

    required property var modelData
    required property int index
    readonly property bool header: modelData.header !== undefined
    // Search results show: each entry names its category.
    property bool searching: false
    // The id of the open page; its row is highlighted.
    property string open

    height: header ? Ui.size.handbookHeading : Ui.size.handbookRow
    // The row draws its own content; the name is for screen readers.
    Accessible.name: header ? modelData.header : modelData.title
    enabled: !header
    highlighted: !header && modelData.id === open

    contentItem: RowLayout {
        spacing: Ui.space.s

        Image {
            Layout.preferredWidth: Ui.size.handbookIcon
            Layout.preferredHeight: Ui.size.handbookIcon
            visible: !entryRow.header
            source: !entryRow.header && entryRow.modelData.icon.length > 0 ? ItemIcons.source(entryRow.modelData.icon, ItemIcons.generation) : ""
            sourceSize: Qt.size(Ui.size.handbookIcon, Ui.size.handbookIcon)
            smooth: false
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Label {
                Layout.fillWidth: true
                text: entryRow.header ? entryRow.modelData.header : entryRow.modelData.title
                color: entryRow.header ? Theme.heading : Theme.text
                font.bold: entryRow.header
                elide: Text.ElideRight
            }

            Label {
                visible: entryRow.searching && !entryRow.header
                text: entryRow.header ? "" : entryRow.modelData.categoryTitle
                color: Theme.textMuted
                font.pixelSize: Ui.type.help
            }
        }
    }
}
