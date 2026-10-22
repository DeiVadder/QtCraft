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

    function number(value: var, decimals: int): string {
        return Number(value ?? 0).toLocaleString(Qt.locale(), "f", decimals);
    }

    // Milliseconds of a tick phase, which are often below one.
    function fine(ms: var): string {
        return Number(ms ?? 0).toLocaleString(Qt.locale(), "f", 2);
    }

    // A duration in milliseconds as days, hours and minutes.
    function duration(ms: var): string {
        const minutes = Math.floor((ms ?? 0) / 60000);
        const days = Math.floor(minutes / 1440);
        const hours = Math.floor(minutes / 60) % 24;
        if (days > 0)
            return qsTr("%1 d %2 h").arg(days).arg(hours);
        return qsTr("%1 h %2 min").arg(hours).arg(minutes % 60);
    }

    title: qsTr("Overview")

    Label {
        Layout.fillWidth: true
        visible: Object.keys(page.status).length === 0
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
                valueText: page.number(page.status.tps, 1)
                values: page.charts.tps ?? []
                capacity: page.capacity
                limit: page.admin.ticksPerSecond
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Tick time, 99th percentile")
                valueText: qsTr("%1 ms (mean %2)").arg(page.number(page.status.tickP99, 1)).arg(page.number(page.status.tickMean, 1))
                values: page.charts.tickP99 ?? []
                capacity: page.capacity
                limit: page.admin.tickBudgetMs
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Players")
                valueText: page.number(page.status.players, 0)
                values: page.charts.players ?? []
                capacity: page.capacity
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Loaded chunks")
                valueText: page.number(page.status.chunks, 0)
                values: page.charts.chunks ?? []
                capacity: page.capacity
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Entities")
                valueText: page.number(page.status.entities, 0)
                values: page.charts.entities ?? []
                capacity: page.capacity
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Memory")
                valueText: qsTr("%1 MB").arg(page.number(page.status.memory, 0))
                values: page.charts.memory ?? []
                capacity: page.capacity
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Network sent")
                valueText: qsTr("%1 KB/s").arg(page.number(page.status.sent, 1))
                values: page.charts.sent ?? []
                capacity: page.capacity
            }
            LineChart {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                title: qsTr("Network received")
                valueText: qsTr("%1 KB/s").arg(page.number(page.status.received, 1))
                values: page.charts.received ?? []
                capacity: page.capacity
            }
        }
    }

    SettingGroup {
        title: qsTr("Slowest tick phases")

        Repeater {
            model: page.status.phases ?? []

            delegate: SettingRow {
                id: phaseRow

                required property var modelData

                label: modelData.name
                Label {
                    text: qsTr("%1 ms").arg(page.fine(phaseRow.modelData.ms))
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
                { label: qsTr("World"), value: page.status.world ?? "" },
                { label: qsTr("Version"), value: qsTr("%1 (protocol %2)").arg(page.status.version ?? "").arg(page.status.protocol ?? 0) },
                { label: qsTr("Running for"), value: page.duration(page.status.uptimeMs) },
                { label: qsTr("Peak memory"), value: qsTr("%1 MB").arg(page.number(page.status.peakMemory, 0)) },
                { label: qsTr("Skipped ticks"), value: page.number(page.status.skipped, 0) },
                { label: qsTr("Seed"), value: page.status.seed ?? "" },
                { label: qsTr("Generator"), value: page.status.generator ?? "" }
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
