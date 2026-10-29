import QtQuick
import QtQuick.Layouts
import QtCraft

// Saving, backups and stopping in "Manage server" (decision E-863). Stopping asks first (AdminActionDialog,
// modal) and is offered only on a server of its own: a world played here ends
// with the pause menu's last button ("Save and quit to menu", "Quit to menu" for a world that is not saved).
SettingsPage {
    id: page

    required property AdminController admin
    // The screen's modal dialog for actions that ask first.
    required property AdminActionDialog dialog
    // The server is a server of its own (else this game hosts the world).
    property bool remote: false
    // The world played here is saved (GameController.persistent): the pause menu's last button saves it.
    property bool persistent: true

    title: qsTr("Server")

    SettingGroup {
        title: qsTr("Saving")

        SettingRow {
            label: qsTr("Save the world")
            help: qsTr("Writes every changed chunk and player now; the server also saves by itself.")

            GameButton {
                objectName: "adminSave"
                text: qsTr("Save now")
                onClicked: page.admin.save()
            }
        }
        SettingRow {
            label: qsTr("Make a backup")
            help: qsTr("A copy of the world in the backups folder. The name is one word; empty uses the date.")

            GameTextField {
                id: backupField

                objectName: "adminBackupName"
                Layout.fillWidth: true
                maximumLength: page.admin.labelLength
                validator: RegularExpressionValidator { regularExpression: /[\w.-]*/ }
                placeholderText: qsTr("Name (optional)")
                Accessible.name: qsTr("Name of the backup")
            }
            GameButton {
                objectName: "adminBackup"
                text: qsTr("Back up")
                onClicked: {
                    page.admin.backup(backupField.text);
                    backupField.text = "";
                }
            }
        }
    }

    SettingGroup {
        title: qsTr("Stopping")

        SettingRow {
            visible: page.remote
            label: qsTr("Stop the server")
            help: qsTr("Saves the world and ends the server for everyone, you included.")

            GameButton {
                objectName: "adminStop"
                kind: GameButton.Destructive
                text: qsTr("Stop …")
                onClicked: page.dialog.ask(qsTr("Stop the server"),
                                        qsTr("Everyone on the server is disconnected, you included. The server saves the world and ends; it starts again only where it runs."),
                                        qsTr("Stop server"), false, () => page.admin.stop())
            }
        }
        Hint {
            Layout.fillWidth: true
            visible: !page.remote
            text: qsTr("This game hosts the world. To end it, use “%1” in the pause menu.")
                  .arg(page.persistent ? qsTr("Save and quit to menu") : qsTr("Quit to menu"))
        }
    }
}
