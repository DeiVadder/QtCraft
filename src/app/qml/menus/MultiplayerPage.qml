pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The main menu's multiplayer page (decisions E-258, E-852): the recently joined servers beside the
// form with address, player name and password, how to play in the local network, the progress of
// the join with a cancel button and the reason of a refusal.
ColumnLayout {
    id: screen

    required property GameController game
    property string failure

    // The page should be shown (open(), also --overlay multiplayer).
    signal openRequested
    // The join succeeded (the game starts).
    signal joined

    // Shows the page with the address field focused and the player name filled in.
    function open() {
        screen.failure = "";
        if (nameField.text.length === 0)
            nameField.text = screen.game.multiplayer.settings.playerName.length > 0 ? screen.game.multiplayer.settings.playerName : qsTr("Player");
        openRequested();
        addressField.forceActiveFocus(Qt.TabFocusReason);
    }

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

    spacing: Ui.space.l

    Connections {
        target: screen.game.multiplayer

        function onJoinFailed(reason: string) {
            screen.failure = reason;
        }
        function onJoined() {
            passwordField.text = "";
            screen.joined();
        }
    }

    // A field's label above it, with an optional help line below.
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
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Keys the controls leave end here; Esc still leaves the page, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: Ui.space.l

        Label {
            text: qsTr("Multiplayer")
            font.pixelSize: Ui.type.title
            font.bold: true
            color: Theme.text
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Ui.space.xl

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Ui.space.s

                Label {
                    text: qsTr("Recent servers")
                    font.pixelSize: Ui.type.section
                    font.bold: true
                    color: Theme.text
                }

                ListView {
                    id: recentList

                    Accessible.name: qsTr("Recent servers")
                    Accessible.role: Accessible.List
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Ui.space.s
                    boundsBehavior: Flickable.StopAtBounds
                    model: screen.game.multiplayer.settings.recentServers
                    ScrollBar.vertical: ThemedScrollBar {}

                    Label {
                        anchors.centerIn: parent
                        width: parent.width
                        visible: recentList.count === 0
                        text: qsTr("Servers you join appear here.")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        font.pixelSize: Ui.type.body
                        color: Theme.textSecondary
                    }

                    delegate: RecentServerRow {
                        // A gutter on the right for the scroll bar.
                        width: ListView.view.width - Ui.space.l
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
                Layout.preferredWidth: Ui.size.formColumn
                Layout.maximumWidth: Ui.size.formColumn
                Layout.alignment: Qt.AlignTop
                enabled: !screen.game.multiplayer.joining
                spacing: Ui.space.xs

                FieldLabel {
                    Layout.topMargin: 0
                    text: qsTr("Server address")
                }

                GameTextField {
                    id: addressField

                    objectName: "serverAddress"
                    Accessible.name: qsTr("Server address")
                    Layout.fillWidth: true
                    placeholderText: qsTr("host, host:port or [IPv6]:port")
                    onAccepted: screen.join()
                }

                FieldLabel {
                    text: qsTr("Player name")
                }

                GameTextField {
                    id: nameField

                    Accessible.name: qsTr("Player name")
                    Layout.fillWidth: true
                    maximumLength: 32
                    onAccepted: screen.join()
                }

                FieldLabel {
                    text: qsTr("Password")
                }

                GameTextField {
                    id: passwordField

                    Accessible.name: qsTr("Password")
                    Accessible.description: qsTr("Only needed if the server has one.")
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    onAccepted: screen.join()
                }

                HelpLine {
                    text: qsTr("Only needed if the server has one.")
                }

                GameButton {
                    Layout.fillWidth: true
                    Layout.topMargin: Ui.space.m
                    kind: GameButton.Primary
                    large: true
                    text: qsTr("Join")
                    enabled: addressField.text.trim().length > 0 && nameField.text.trim().length > 0
                    onClicked: screen.join()
                }

                // Games in the local network: a player opens a running world from the pause menu.
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: Ui.space.xl
                    text: qsTr("In the local network")
                    font.pixelSize: Ui.type.group
                    font.bold: true
                    color: Theme.heading
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }
                Hint {
                    Layout.fillWidth: true
                    text: qsTr("Whoever plays a world opens it with “Open to LAN” in the pause menu and tells you one of the addresses it shows. To open your own world, start it and do the same.")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Ui.space.m

            BusyIndicator {
                Layout.preferredWidth: Ui.size.button
                Layout.preferredHeight: Ui.size.button
                visible: screen.game.multiplayer.joining
                running: visible
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: Ui.type.body
                color: screen.failure.length > 0 && !screen.game.multiplayer.joining ? Theme.error : Theme.text
                textFormat: Text.PlainText
                text: screen.game.multiplayer.joinStage === NetworkClient.Stage.Connecting ? qsTr("Connecting to %1 …").arg(screen.game.multiplayer.lastAddress)
                      : screen.game.multiplayer.joinStage === NetworkClient.Stage.LoggingIn ? qsTr("Logging in …")
                      : screen.failure.length > 0 ? qsTr("Could not join: %1").arg(screen.failure)
                      : ""
            }

            // The server showed another certificate than the pinned one (decision E-872).
            GameButton {
                objectName: "forgetCertificateButton"
                visible: !screen.game.multiplayer.joining && screen.failure.length > 0 && screen.game.multiplayer.certificateChangedAddress.length > 0
                large: true
                text: qsTr("Forget the old certificate")
                onClicked: {
                    if (screen.game.multiplayer.forgetCertificate(screen.game.multiplayer.certificateChangedAddress))
                        screen.failure = qsTr("The old certificate is forgotten. Join again only if you are sure the server's owner made a new one.");
                }
            }

            GameButton {
                objectName: "cancelJoinButton"
                visible: screen.game.multiplayer.joining
                large: true
                text: qsTr("Cancel")
                onClicked: screen.game.multiplayer.cancelJoin()
            }

        }
    }
}
