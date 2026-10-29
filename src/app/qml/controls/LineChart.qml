import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// A small line chart of a value over time (decision E-863, docs/UI-Leitfaden.md 6.12): the title
// and the latest value with its unit above, the line on the raised surface below with the top of
// its scale at the upper left, the newest value at the right. An optional limit (the ticks a
// second the server aims at, a tick's time) is a line of its own. DebugGraph draws both in the
// scene graph (no canvas); the scale rounds the highest value up to 1, 2 or 5 times a power of
// ten. Until the first measurement (`measured` false) the value is "–" and the field says so
// instead of drawing a line of zeros (decision E-931).
ColumnLayout {
    id: chart

    property string title
    // The latest value as the player reads it ("19,8", "3,2 ms").
    property string valueText
    property list<real> values
    // The number of values that fill the width (the span: 600 seconds, 1 440 minutes).
    property int capacity: 600
    // Drawn as a line of its own; 0 for none.
    property real limit: 0
    // False until the first value came: the chart shows "–" and a placeholder.
    property bool measured: true
    // The scale reaches at least this far (a flat line of zeros still has a scale).
    property real minimumScale: 1
    readonly property real scaleTop: niceScale(Math.max(minimumScale, limit, highest(values)))

    function highest(list: list<real>): real {
        let found = 0;
        for (let i = 0; i < list.length; ++i)
            found = Math.max(found, list[i]);
        return found;
    }

    // `value` rounded up to 1, 2 or 5 times a power of ten.
    function niceScale(value: real): real {
        const power = Math.pow(10, Math.floor(Math.log10(value)));
        for (const step of [1, 2, 5, 10]) {
            if (value <= step * power)
                return step * power;
        }
        return 10 * power;
    }

    spacing: Ui.space.xs
    Accessible.role: Accessible.Chart
    Accessible.name: qsTr("%1: %2").arg(title).arg(measured ? valueText : qsTr("no measurement yet"))

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.space.s

        Label {
            Layout.fillWidth: true
            text: chart.title
            font.pixelSize: Ui.type.help
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
        Label {
            text: chart.measured ? chart.valueText : "–"
            font.pixelSize: Ui.type.body
            font.bold: true
            color: Theme.text
        }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Ui.size.chart
        radius: Ui.radius
        color: Theme.panelRaised
        border.width: Ui.size.line
        // Quieter than the line: the frame only bounds the field.
        border.color: Theme.separator

        DebugGraph {
            visible: chart.measured
            anchors.fill: parent
            anchors.margins: Ui.space.xs
            values: chart.values
            capacity: Math.max(chart.capacity, chart.values.length)
            maxValue: chart.scaleTop
            limit: chart.limit
            color: Theme.accent
            limitColor: Theme.graphLimit
        }
        Label {
            anchors.centerIn: parent
            visible: !chart.measured
            text: qsTr("No measurement yet")
            font.pixelSize: Ui.type.help
            color: Theme.textMuted
        }
        Label {
            visible: chart.measured
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: Ui.space.xs
            text: chart.scaleTop.toLocaleString(Qt.locale(), "f", chart.scaleTop < 1 ? 1 : 0)
            font.pixelSize: Ui.type.badge
            color: Theme.textMuted
        }
    }
}
