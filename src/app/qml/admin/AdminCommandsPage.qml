pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Commands in "Manage server" (decision E-863): a command line with completion from the server's
// command table, the players and the content (Tab takes the first suggestion, the suggestions are
// buttons for the mouse and the pad), help for the command and its next parameter, and the output
// of the commands run.
ColumnLayout {
    id: page

    required property AdminController admin
    // Both follow the command table (catalogueRevision) and the players (names complete).
    readonly property var suggestions: {
        void admin.catalogueRevision;
        void admin.players;
        return commandField.text.length > 0 ? admin.complete(commandField.text) : [];
    }
    readonly property var help: {
        void admin.catalogueRevision;
        return admin.help(commandField.text);
    }

    function run() {
        page.admin.runCommand(commandField.text);
        commandField.text = "";
    }

    // Takes a suggestion and goes on writing after it.
    function take(line: string) {
        commandField.text = line + " ";
        commandField.forceActiveFocus();
        commandField.cursorPosition = commandField.text.length;
    }

    spacing: Ui.space.m

    Label {
        text: qsTr("Commands")
        font.pixelSize: Ui.type.section
        font.bold: true
        color: Theme.text
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Ui.space.s

        GameTextField {
            id: commandField

            objectName: "adminCommandField"
            Layout.fillWidth: true
            maximumLength: page.admin.textLength
            placeholderText: qsTr("/command, Tab completes")
            Accessible.name: qsTr("Command line")
            onAccepted: page.run()
            Keys.onTabPressed: event => {
                if (page.suggestions.length > 0 && page.suggestions[0].trim() !== text.trim())
                    page.take(page.suggestions[0]);
                else
                    event.accepted = false;
            }
        }
        GameButton {
            objectName: "adminCommandRun"
            kind: GameButton.Primary
            text: qsTr("Run")
            enabled: commandField.text.trim().length > 0
            onClicked: page.run()
        }
    }

    Flow {
        Layout.fillWidth: true
        visible: page.suggestions.length > 0
        spacing: Ui.space.xs

        Repeater {
            model: page.suggestions

            delegate: GameButton {
                required property string modelData

                kind: GameButton.Quiet
                text: modelData
                Accessible.name: qsTr("Complete to %1").arg(modelData)
                onClicked: page.take(modelData)
            }
        }
    }

    Hint {
        Layout.fillWidth: true
        visible: page.help.command !== undefined
        text: page.help.command === undefined ? ""
            : (page.help.next.length > 0 ? qsTr("%1\n%2\nNext: %3") : qsTr("%1\n%2"))
                .arg(page.help.usage).arg(page.help.description).arg(page.help.next)
    }

    AdminLineList {
        title: qsTr("Command output")
        lines: page.admin.output
        emptyText: qsTr("Commands you run and the server's answers show here.")
    }

    RowLayout {
        Layout.fillWidth: true

        Item {
            Layout.fillWidth: true
        }
        GameButton {
            objectName: "adminClearOutput"
            kind: GameButton.Quiet
            text: qsTr("Clear output")
            enabled: page.admin.output.count > 0
            onClicked: page.admin.clearOutput()
        }
    }
}
