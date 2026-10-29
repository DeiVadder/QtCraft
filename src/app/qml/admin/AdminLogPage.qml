// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The server's log in "Manage server" (decision E-863): its recent lines from the chosen level on
// and each new one while the screen is open; choosing another level fetches the tail at it. With
// "Follow" the view stays at the newest line.
ColumnLayout {
    id: page

    required property AdminController admin

    spacing: Ui.space.m

    Label {
        text: qsTr("Log")
        font.pixelSize: Ui.type.section
        font.bold: true
        color: Theme.text
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.space.l

        Segments {
            objectName: "adminLogLevel"
            label: qsTr("Level")
            model: [{ value: AdminController.LogLevel.Debug, text: qsTr("All") },
                { value: AdminController.LogLevel.Info, text: qsTr("Info") },
                { value: AdminController.LogLevel.Warning, text: qsTr("Warnings") },
                { value: AdminController.LogLevel.Critical, text: qsTr("Errors") }]
            currentValue: page.admin.logLevel
            onActivated: value => page.admin.logLevel = value
        }
        Item {
            Layout.fillWidth: true
        }
        Label {
            text: qsTr("Follow")
            font.pixelSize: Ui.type.body
            color: Theme.text
        }
        GameSwitch {
            id: followSwitch

            objectName: "adminLogFollow"
            checked: true
            Accessible.name: qsTr("Follow the newest line")
        }
    }

    AdminLineList {
        title: qsTr("Log lines")
        lines: page.admin.log
        follow: followSwitch.checked
        emptyText: qsTr("No log lines at this level yet.")
    }
}
