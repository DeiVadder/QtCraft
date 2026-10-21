pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The log file and crash reports on the debug page (decision E-834): switching the log file on or
// off for the next start, opening the folder of logs and crash reports, and collecting a
// diagnostics bundle to hand in after a playtest.
ColumnLayout {
    id: section

    required property GameController game
    // The folder of the latest bundle; empty until one was written.
    property string bundle: ""
    property bool bundleFailed: false

    spacing: Ui.px(8)

    Label {
        text: qsTr("Log and crash reports")
        font.bold: true
    }

    CheckBox {
        objectName: "logToFile"
        text: qsTr("Write a log file (from the next start)")
        checked: section.game.settings.diagnostics.logToFile
        onToggled: section.game.settings.diagnostics.logToFile = checked
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.px(12)

        Button {
            objectName: "openLogFolder"
            text: qsTr("Open log folder")
            onClicked: section.game.settings.diagnostics.openLogFolder()
        }
        Button {
            objectName: "collectDiagnostics"
            text: section.game.settings.diagnostics.collecting ? qsTr("Collecting…") : qsTr("Collect diagnostics")
            enabled: !section.game.settings.diagnostics.collecting
            onClicked: section.game.settings.diagnostics.collectDiagnostics()
        }
    }

    Connections {
        target: section.game.settings.diagnostics

        function onDiagnosticsCollected(folder: string) {
            section.bundle = folder
            section.bundleFailed = folder === ""
        }
    }

    Label {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        text: section.bundleFailed ? qsTr("The diagnostics could not be written; the log says why.")
              : section.bundle !== "" ? qsTr("Diagnostics written to %1. Send this folder (zipped) to the organiser.").arg(section.bundle)
              : qsTr("Logs and crash reports are in %1.").arg(section.game.settings.diagnostics.logFolder)
        wrapMode: Text.Wrap
        opacity: 0.7
    }
}
