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
    width: Math.min(parent.width - Ui.px(64), Ui.px(520))
    height: Math.min(implicitHeight, parent.height - Ui.px(32))
    modal: true
    focus: true
    padding: Ui.px(20)

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

    ColumnLayout {
        anchors.fill: parent
        focus: true
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: Ui.px(8)

        Label {
            text: qsTr("Open to LAN")
            font.pixelSize: Ui.px(24)
            font.bold: true
        }

        // Before opening: the settings of the listener.
        ColumnLayout {
            Layout.fillWidth: true
            visible: screen.game.multiplayer.lanPort === 0
            spacing: Ui.px(6)

            Label {
                Layout.fillWidth: true
                text: qsTr("Other players can join this world while you play. It stays open until you stop hosting or leave the world.")
                wrapMode: Text.Wrap
                color: Theme.textSecondary
            }

            Label {
                text: qsTr("Port (0: any free port)")
            }

            TextField {
                id: portField

                Accessible.name: qsTr("Port (0: any free port)")
                Layout.fillWidth: true
                validator: IntValidator {
                    bottom: 0
                    top: 65535
                }
                inputMethodHints: Qt.ImhDigitsOnly
                selectByMouse: true
            }

            Label {
                text: qsTr("Password (empty: anyone may join)")
            }

            TextField {
                id: passwordField

                Accessible.name: qsTr("Password (empty: anyone may join)")
                Layout.fillWidth: true
                echoMode: TextInput.Password
                selectByMouse: true
            }

            Label {
                text: qsTr("Players at most")
            }

            SpinBox {
                id: maxPlayers

                Accessible.name: qsTr("Players at most")
                from: 1
                to: 64
                editable: true
            }

            Label {
                Layout.fillWidth: true
                visible: screen.failure.length > 0
                textFormat: Text.PlainText
                text: qsTr("Could not open the world: %1").arg(screen.failure)
                wrapMode: Text.Wrap
                color: Theme.error
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Ui.px(8)

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Open to other players")
                    highlighted: true
                    enabled: portField.acceptableInput || portField.text.length === 0
                    onClicked: screen.openWorld()
                }

                Button {
                    text: qsTr("Cancel")
                    onClicked: screen.close()
                }
            }
        }

        // While open: where friends connect.
        ColumnLayout {
            Layout.fillWidth: true
            visible: screen.game.multiplayer.lanPort > 0
            spacing: Ui.px(6)

            Label {
                Layout.fillWidth: true
                text: qsTr("The world is open on port %1.").arg(screen.game.multiplayer.lanPort)
                font.bold: true
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Friends in your network choose Multiplayer in the main menu and join with one of these addresses:")
                wrapMode: Text.Wrap
            }

            Repeater {
                model: screen.addresses

                delegate: TextField {
                    required property string modelData

                    Layout.fillWidth: true
                    text: modelData
                    readOnly: true
                    selectByMouse: true
                    font.family: "Menlo"
                }
            }

            Label {
                Layout.fillWidth: true
                visible: screen.addresses.length === 0
                text: qsTr("This computer has no network address other players could reach.")
                wrapMode: Text.Wrap
                color: Theme.warning
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("The firewall may ask whether QtCraft may accept incoming connections: allow it. Players over the internet need the port forwarded in your router (see the handbook).")
                wrapMode: Text.Wrap
                color: Theme.textSecondary
                font.pixelSize: Ui.px(12)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Ui.px(8)

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Stop hosting")
                    onClicked: screen.game.multiplayer.closeLan()
                }

                Button {
                    text: qsTr("Close")
                    highlighted: true
                    onClicked: screen.close()
                }
            }
        }
    }
}
