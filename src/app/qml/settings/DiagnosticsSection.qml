pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// The log file and crash reports on the advanced page (decision E-834): switching the log file on
// or off for the next start, opening the folder of logs and crash reports, and collecting a
// diagnostics bundle to hand in after a playtest. The folder's path stands below the button as
// selectable text (docs/UI-Leitfaden.md 5).
SettingGroup {
    id: section

    required property GameController game
    // The folder of the latest bundle; empty until one was written.
    property string bundle: ""
    property bool bundleFailed: false

    title: qsTr("Log and crash reports")

    SettingSwitch {
        objectName: "logToFile"
        label: qsTr("Write a log file")
        badge: qsTr("from the next start")
        checked: section.game.settings.diagnostics.logToFile
        onToggled: section.game.settings.diagnostics.logToFile = checked
    }

    SettingRow {
        label: qsTr("Folder")

        GameButton {
            objectName: "openLogFolder"
            text: qsTr("Open log folder")
            onClicked: section.game.settings.diagnostics.openLogFolder()
        }
        Item {
            Layout.fillWidth: true
        }
    }

    // The folder's path, to copy into a message or a file dialog: selectable, broken anywhere.
    TextEdit {
        objectName: "logFolderPath"
        Layout.fillWidth: true
        readOnly: true
        selectByMouse: true
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.WrapAnywhere
        text: section.game.settings.diagnostics.logFolder
        font.pixelSize: Ui.type.help
        color: Theme.textMuted
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Log folder: %1").arg(text)
    }

    SettingRow {
        label: qsTr("Diagnostics")
        help: qsTr("Logs and crash reports in one folder, to hand in after a playtest")

        GameButton {
            objectName: "collectDiagnostics"
            text: section.game.settings.diagnostics.collecting ? qsTr("Collecting…") : qsTr("Collect diagnostics")
            enabled: !section.game.settings.diagnostics.collecting
            onClicked: section.game.settings.diagnostics.collectDiagnostics()
        }
        Item {
            Layout.fillWidth: true
        }
    }

    Connections {
        target: section.game.settings.diagnostics

        function onDiagnosticsCollected(folder: string) {
            section.bundle = folder
            section.bundleFailed = folder === ""
        }
    }

    Hint {
        Layout.fillWidth: true
        Layout.bottomMargin: Ui.space.xs
        visible: section.bundleFailed || section.bundle !== ""
        kind: section.bundleFailed ? Hint.Error : Hint.Info
        text: section.bundleFailed ? qsTr("The diagnostics could not be written; the log says why.")
              : qsTr("Diagnostics written to %1. Send this folder (zipped) to the organiser.").arg(section.bundle)
    }
}
