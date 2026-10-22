pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Opening the running world to other players (decision E-260): port, optional password and player
// limit; once open, the port and the addresses friends join with, and stopping again.
Popup {
    id: screen

    required property GameController game
    property string failure
    // Asked again whenever the port changes or the screen opens.
    readonly property list<string> addresses: visible && game.multiplayer.lanPort > 0 ? game.multiplayer.lanAddresses() : []

    function openWorld() {
        screen.failure = "";
        const port = portField.text.length > 0 ? parseInt(portField.text) : 0;
        screen.game.multiplayer.openToLan(port, passwordField.text, maxPlayers.value);
        passwordField.text = "";
    }

    objectName: "lanScreen"
    anchors.centerIn: parent
    width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.s)
    height: Math.min(implicitHeight, parent.height - 2 * Ui.screen.marginVertical)
    modal: true
    background: ScreenFrame {}
    focus: true
    padding: Ui.space.xl

    onAboutToShow: {
        screen.failure = "";
        portField.text = String(screen.game.multiplayer.settings.hostPort);
        maxPlayers.value = screen.game.multiplayer.settings.hostMaxPlayers;
    }

    Connections {
        target: screen.game.multiplayer

        function onLanFailed(reason: string) {
            screen.failure = reason;
        }
    }

    // A field's label above it, and a help line below it.
    component FieldLabel: Label {
        Layout.fillWidth: true
        Layout.topMargin: Ui.space.s
        font.pixelSize: Ui.type.body
        color: Theme.text
        wrapMode: Text.Wrap
    }
    component HelpLine: Label {
        Layout.fillWidth: true
        font.pixelSize: Ui.type.help
        color: Theme.textMuted
        wrapMode: Text.Wrap
    }

    ColumnLayout {
        anchors.fill: parent
        focus: true
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: Ui.space.m

        Label {
            text: qsTr("Open to LAN")
            font.pixelSize: Ui.type.title
            font.bold: true
            color: Theme.text
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        // Before opening: the settings of the listener.
        ColumnLayout {
            Layout.fillWidth: true
            visible: screen.game.multiplayer.lanPort === 0
            spacing: Ui.space.xs

            Label {
                Layout.fillWidth: true
                text: qsTr("Other players can join this world while you play. It stays open until you stop hosting or leave the world.")
                wrapMode: Text.Wrap
                font.pixelSize: Ui.type.body
                color: Theme.textSecondary
            }

            FieldLabel {
                text: qsTr("Port")
            }

            GameTextField {
                id: portField

                Accessible.name: qsTr("Port")
                Accessible.description: qsTr("0 takes any free port.")
                Layout.fillWidth: true
                validator: IntValidator {
                    bottom: 0
                    top: 65535
                }
                inputMethodHints: Qt.ImhDigitsOnly
            }

            HelpLine {
                text: qsTr("0 takes any free port.")
            }

            FieldLabel {
                text: qsTr("Password")
            }

            GameTextField {
                id: passwordField

                Accessible.name: qsTr("Password")
                Accessible.description: qsTr("Empty: anyone may join.")
                Layout.fillWidth: true
                echoMode: TextInput.Password
            }

            HelpLine {
                text: qsTr("Empty: anyone may join.")
            }

            FieldLabel {
                text: qsTr("Players at most")
            }

            SpinBox {
                id: maxPlayers

                Accessible.name: qsTr("Players at most")
                implicitHeight: Ui.size.field
                font.pixelSize: Ui.type.body
                from: 1
                to: 64
                editable: true
            }

            Hint {
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.s
                visible: screen.failure.length > 0
                kind: Hint.Error
                text: qsTr("Could not open the world: %1").arg(screen.failure)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.l
                spacing: Ui.space.s

                Item {
                    Layout.fillWidth: true
                }
                GameButton {
                    large: true
                    text: qsTr("Cancel")
                    onClicked: screen.close()
                }
                GameButton {
                    kind: GameButton.Primary
                    large: true
                    text: qsTr("Open to other players")
                    enabled: portField.acceptableInput || portField.text.length === 0
                    onClicked: screen.openWorld()
                }
            }
        }

        // While open: where friends connect.
        ColumnLayout {
            Layout.fillWidth: true
            visible: screen.game.multiplayer.lanPort > 0
            spacing: Ui.space.s

            Label {
                Layout.fillWidth: true
                text: qsTr("The world is open on port %1.").arg(screen.game.multiplayer.lanPort)
                font.pixelSize: Ui.type.body
                font.bold: true
                color: Theme.text
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Friends in your network choose Multiplayer in the main menu and join with one of these addresses:")
                font.pixelSize: Ui.type.body
                color: Theme.text
                wrapMode: Text.Wrap
            }

            Repeater {
                model: screen.addresses

                delegate: GameTextField {
                    required property string modelData

                    Layout.fillWidth: true
                    Accessible.name: qsTr("Address %1").arg(modelData)
                    text: modelData
                    readOnly: true
                    font.family: "Menlo"
                }
            }

            Hint {
                Layout.fillWidth: true
                visible: screen.addresses.length === 0
                kind: Hint.Warning
                text: qsTr("This computer has no network address other players could reach.")
            }

            HelpLine {
                text: qsTr("The firewall may ask whether QtCraft may accept incoming connections: allow it. Players over the internet need the port forwarded in your router (see the handbook).")
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.l
                spacing: Ui.space.s

                GameButton {
                    large: true
                    text: qsTr("Stop hosting")
                    onClicked: screen.game.multiplayer.closeLan()
                }
                Item {
                    Layout.fillWidth: true
                }
                GameButton {
                    kind: GameButton.Primary
                    large: true
                    text: qsTr("Close")
                    onClicked: screen.close()
                }
            }
        }
    }
}
