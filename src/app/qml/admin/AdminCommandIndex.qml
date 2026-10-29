// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The server's commands in "Manage server" (decision E-911): a search field and every command of
// the server's table under its group (AdminCommandList), with its description; clicking one (or
// Enter, Space, the pad's A) hands it to `picked`, which puts it into the command line.
ColumnLayout {
    id: commandIndex

    required property AdminCommandList commands

    // A command was chosen: "/name".
    signal picked(string line)

    spacing: Ui.space.s

    GameTextField {
        id: search

        objectName: "adminCommandSearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Search commands")
        Accessible.name: qsTr("Search the commands")
        text: commandIndex.commands.filter
        onTextChanged: commandIndex.commands.filter = text
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: Ui.size.lines
        radius: Ui.radius
        color: Theme.field

        Bevel {
            sunken: true
        }

        ListView {
            id: list

            objectName: "adminCommandIndex"
            anchors.fill: parent
            anchors.margins: Ui.space.s
            clip: true
            // Compact rows stand closer than other lists of rows (UI-Leitfaden 6.9).
            spacing: Ui.space.xs
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Commands of the server")
            model: commandIndex.commands
            ScrollBar.vertical: ThemedScrollBar {}

            // Moves the focus to row `next`, scrolled into view and made if it was not yet (the
            // focus chain only knows the rows that exist); false past either end, so Tab goes on to
            // the next control.
            function focusRow(next: int): bool {
                if (next < 0 || next >= count)
                    return false;
                positionViewAtIndex(next, ListView.Contain);
                const target = itemAtIndex(next);
                if (!target)
                    return false;
                target.forceActiveFocus(Qt.TabFocusReason);
                return true;
            }

            section.property: "group"
            section.delegate: Label {
                required property string section

                width: ListView.view.width - Ui.space.l
                topPadding: Ui.space.s
                bottomPadding: Ui.space.xs
                text: section
                font.pixelSize: Ui.type.body
                font.bold: true
                color: Theme.heading
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            delegate: ListRow {
                id: row

                // The roles of AdminCommandList: name, usage, description, group, operatorOnly.
                required property var model
                required property int index

                objectName: "adminCommand_" + model.name
                width: ListView.view.width - Ui.space.l
                interactive: true
                focusable: true
                // About forty commands: one line each, the description whole with the focus.
                compact: true
                title: "/" + model.name
                subtitle: model.description
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("/%1: %2").arg(model.name).arg(model.description)
                onClicked: commandIndex.picked("/" + row.model.name)
                Keys.onTabPressed: event => event.accepted = list.focusRow(row.index + 1)
                Keys.onBacktabPressed: event => event.accepted = list.focusRow(row.index - 1)
                Keys.onDownPressed: event => event.accepted = list.focusRow(row.index + 1)
                Keys.onUpPressed: event => event.accepted = list.focusRow(row.index - 1)
                // The focused row grows (its description whole): it stays in view.
                onHeightChanged: {
                    if (activeFocus)
                        list.positionViewAtIndex(row.index, ListView.Contain);
                }
                // A row reached otherwise (the pad, a click) scrolls into view as well.
                onActiveFocusChanged: {
                    if (activeFocus) {
                        list.currentIndex = row.index;
                        list.positionViewAtIndex(row.index, ListView.Contain);
                    }
                }
            }
        }

        Label {
            anchors.centerIn: parent
            width: parent.width - 2 * Ui.space.l
            visible: list.count === 0
            text: commandIndex.commands.total === 0 ? qsTr("The server's commands show here once it sent them.")
                                             : qsTr("No command matches \"%1\".").arg(commandIndex.commands.filter)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Ui.type.body
            color: Theme.textMuted
        }
    }
}
