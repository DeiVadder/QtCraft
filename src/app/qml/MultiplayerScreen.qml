pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Joining a network game (decision E-258): address, player name and password, the progress of the
// join with a cancel button, the reason of a refusal, and the recently joined servers.
Popup {
    id: screen

    required property GameController game
    property string failure

    // Starts a join from the fields (also the --overlay multiplayer test hook with --join).
    function joinServer(address: string, name: string, password: string) {
        addressField.text = address;
        nameField.text = name;
        passwordField.text = password;
        screen.join();
    }

    function join() {
        screen.failure = "";
        screen.game.multiplayer.join(addressField.text, nameField.text, passwordField.text);
    }

    anchors.centerIn: parent
    width: Math.min(parent.width - 64, 820)
    height: Math.min(parent.height - 64, 560)
    modal: true
    focus: true
    padding: 20
    closePolicy: screen.game.multiplayer.joining ? Popup.NoAutoClose : Popup.CloseOnEscape

    onAboutToShow: {
        screen.failure = "";
        if (nameField.text.length === 0)
            nameField.text = screen.game.multiplayer.settings.playerName.length > 0 ? screen.game.multiplayer.settings.playerName : qsTr("Player");
        addressField.forceActiveFocus();
    }

    Connections {
        target: screen.game.multiplayer

        function onJoinFailed(reason: string) {
            screen.failure = reason;
        }
        function onJoined() {
            passwordField.text = "";
            screen.close();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        focus: true
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: 12

        Label {
            text: qsTr("Multiplayer")
            font.pixelSize: 24
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 420
                spacing: 6

                Label {
                    text: qsTr("Recent servers")
                    font.pixelSize: 18
                }

                ListView {
                    id: recentList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: screen.game.multiplayer.settings.recentServers

                    Label {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: recentList.count === 0
                        text: qsTr("Servers you join appear here.")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.textSecondary
                    }

                    delegate: RecentServerRow {
                        width: ListView.view.width
                        enabled: !screen.game.multiplayer.joining
                        onChosen: address => addressField.text = address
                        onJoinRequested: address => {
                            addressField.text = address;
                            screen.join();
                        }
                        onRemoveRequested: address => screen.game.multiplayer.settings.forgetServer(address)
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 300
                Layout.maximumWidth: 300
                Layout.alignment: Qt.AlignTop
                enabled: !screen.game.multiplayer.joining
                spacing: 6

                Label {
                    text: qsTr("Server address")
                }

                TextField {
                    id: addressField

                    objectName: "serverAddress"
                    Layout.fillWidth: true
                    placeholderText: qsTr("host, host:port or [IPv6]:port")
                    selectByMouse: true
                    onAccepted: screen.join()
                }

                Label {
                    text: qsTr("Player name")
                }

                TextField {
                    id: nameField

                    Layout.fillWidth: true
                    maximumLength: 32
                    selectByMouse: true
                    onAccepted: screen.join()
                }

                Label {
                    text: qsTr("Password (only if the server has one)")
                }

                TextField {
                    id: passwordField

                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    selectByMouse: true
                    onAccepted: screen.join()
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Passwords are never saved. The game connects directly to the address, without encryption.")
                    wrapMode: Text.Wrap
                    color: Theme.textSecondary
                    font.pixelSize: 12
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Join")
                    highlighted: true
                    enabled: addressField.text.trim().length > 0 && nameField.text.trim().length > 0
                    onClicked: screen.join()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            BusyIndicator {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                visible: screen.game.multiplayer.joining
                running: visible
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: screen.failure.length > 0 && !screen.game.multiplayer.joining ? Theme.error : Theme.text
                textFormat: Text.PlainText
                text: screen.game.multiplayer.joinStage === NetworkClient.Stage.Connecting ? qsTr("Connecting to %1 …").arg(screen.game.multiplayer.lastAddress)
                      : screen.game.multiplayer.joinStage === NetworkClient.Stage.LoggingIn ? qsTr("Logging in …")
                      : screen.failure.length > 0 ? qsTr("Could not join: %1").arg(screen.failure)
                      : ""
            }

            Button {
                visible: screen.game.multiplayer.joining
                text: qsTr("Cancel")
                onClicked: screen.game.multiplayer.cancelJoin()
            }

            Button {
                visible: !screen.game.multiplayer.joining
                text: qsTr("Close")
                onClicked: screen.close()
            }
        }
    }
}
