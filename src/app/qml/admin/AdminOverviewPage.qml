// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The overview of "Manage server" (decision E-863): the server's load as small charts of the last
// ten minutes or the day (ticks a second, a tick's time, players, chunks, entities, memory,
// network), the slowest tick phases and what the server is (world, version, uptime).
SettingsPage {
    id: page

    required property AdminController admin
    readonly property var status: admin.status
    readonly property var charts: admin.charts
    readonly property int capacity: admin.chartCapacity
    // No status came yet: every value shows "–" instead of a zero (AD-5 of docs/Menue-Pruefung.md);
    // the texts come formatted from C++ (QcAdminViews::status).
    readonly property bool measured: status.measured

    title: qsTr("Overview")

    Label {
        Layout.fillWidth: true
        visible: !page.measured
        text: qsTr("Waiting for the server's status …")
        font.pixelSize: Ui.type.body
        color: Theme.textMuted
    }

    SettingGroup {
        title: qsTr("Load")

        Segments {
            objectName: "adminChartSpan"
            label: qsTr("Span")
            model: [{ value: false, text: qsTr("10 minutes") }, { value: true, text: qsTr("24 hours") }]
            currentValue: page.admin.minutes
            onActivated: value => page.admin.minutes = value
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Ui.space.l
            rowSpacing: Ui.space.m

            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Ticks per second")
                valueText: page.status.tpsText
                values: page.charts.tps ?? []
                capacity: page.capacity
                measured: page.measured
                limit: page.admin.ticksPerSecond
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Tick time, 99th percentile")
                valueText: page.status.tickText
                values: page.charts.tickP99 ?? []
                capacity: page.capacity
                measured: page.measured
                limit: page.admin.tickBudgetMs
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Players")
                valueText: page.status.playersText
                values: page.charts.players ?? []
                capacity: page.capacity
                measured: page.measured
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Loaded chunks")
                valueText: page.status.chunksText
                values: page.charts.chunks ?? []
                capacity: page.capacity
                measured: page.measured
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Entities")
                valueText: page.status.entitiesText
                values: page.charts.entities ?? []
                capacity: page.capacity
                measured: page.measured
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Memory")
                valueText: page.status.memoryText
                values: page.charts.memory ?? []
                capacity: page.capacity
                measured: page.measured
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Network sent")
                valueText: page.status.sentText
                values: page.charts.sent ?? []
                capacity: page.capacity
                measured: page.measured
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Network received")
                valueText: page.status.receivedText
                values: page.charts.received ?? []
                capacity: page.capacity
                measured: page.measured
            }
        }
    }

    SettingGroup {
        title: qsTr("Slowest tick phases")

        Repeater {
            model: page.status.phases

            delegate: SettingRow {
                id: phaseRow

                required property var modelData

                label: modelData.name
                Label {
                    text: phaseRow.modelData.msText
                    font.pixelSize: Ui.type.body
                    color: Theme.text
                }
            }
        }
    }

    SettingGroup {
        title: qsTr("Server")

        Repeater {
            model: [
                { label: qsTr("World"), value: page.status.world },
                { label: qsTr("Version"), value: page.status.versionText },
                { label: qsTr("Running for"), value: page.status.uptimeText },
                { label: qsTr("Peak memory"), value: page.status.peakMemoryText },
                { label: qsTr("Skipped ticks"), value: page.status.skippedText },
                { label: qsTr("Seed"), value: page.status.seed },
                { label: qsTr("Generator"), value: page.status.generator }
            ]

            delegate: SettingRow {
                id: infoRow

                required property var modelData

                label: modelData.label
                Label {
                    Layout.fillWidth: true
                    text: infoRow.modelData.value
                    textFormat: Text.PlainText
                    font.pixelSize: Ui.type.body
                    color: Theme.text
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
