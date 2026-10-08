pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Rectangle {
    id: menu

    required property GameController game

    signal quitRequested

    gradient: Gradient {
        GradientStop { position: 0.0; color: "#5f9bd1" }
        GradientStop { position: 1.0; color: "#2f4f6e" }
    }

    onVisibleChanged: if (visible) game.refreshWorlds()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("QtCraft")
            color: "white"
            font.pixelSize: 64
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 900
            Layout.alignment: Qt.AlignHCenter
            spacing: 24

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 540
                spacing: 8

                Label {
                    text: qsTr("Your worlds")
                    color: "white"
                    font.pixelSize: 20
                }

                ListView {
                    id: worldList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: menu.game.worlds

                    Label {
                        anchors.centerIn: parent
                        visible: worldList.count === 0
                        text: qsTr("No saved worlds yet.")
                        color: "#e0ffffff"
                    }

                    delegate: Rectangle {
                        id: worldRow

                        required property var modelData

                        width: ListView.view.width
                        height: 64
                        radius: 4
                        color: "#40000000"

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    text: worldRow.modelData.name
                                    color: "white"
                                    font.pixelSize: 17
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: worldRow.modelData.error.length > 0
                                          ? worldRow.modelData.error
                                          : qsTr("Last played %1 · Seed %2").arg(worldRow.modelData.lastPlayed).arg(worldRow.modelData.seed)
                                    color: worldRow.modelData.error.length > 0 ? "#ffb0b0" : "#d0ffffff"
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
                                text: qsTr("Play")
                                highlighted: true
                                enabled: worldRow.modelData.error.length === 0
                                onClicked: menu.game.playWorld(worldRow.modelData.folder)
                            }

                            Button {
                                text: qsTr("Delete")
                                onClicked: {
                                    deleteDialog.folder = worldRow.modelData.folder;
                                    deleteDialog.worldName = worldRow.modelData.name;
                                    deleteDialog.open();
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: 320
                Layout.maximumWidth: 320
                Layout.alignment: Qt.AlignTop
                spacing: 8

                Label {
                    text: qsTr("New world")
                    color: "white"
                    font.pixelSize: 20
                }

                Label {
                    text: qsTr("World name")
                    color: "white"
                }

                TextField {
                    id: worldName

                    Layout.fillWidth: true
                    text: qsTr("New World")
                    selectByMouse: true
                }

                Label {
                    text: qsTr("Seed (number or text, empty for random)")
                    color: "white"
                }

                TextField {
                    id: seed

                    Layout.fillWidth: true
                    selectByMouse: true
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Create and play")
                    highlighted: true
                    onClicked: menu.game.createWorld(worldName.text,
                                                     seed.text.length > 0 ? seed.text : String(Math.floor(Math.random() * 2147483647)))
                }

                Item {
                    Layout.preferredHeight: 24
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Settings")
                    onClicked: settingsScreen.open()
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Mods")
                    onClicked: modsScreen.open()
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Quit")
                    onClicked: menu.quitRequested()
                }
            }
        }
    }

    SettingsScreen {
        id: settingsScreen

        game: menu.game
    }

    ModsScreen {
        id: modsScreen

        game: menu.game
    }

    Dialog {
        id: deleteDialog

        property string folder
        property string worldName

        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete world")
        standardButtons: Dialog.Yes | Dialog.Cancel

        Label {
            text: qsTr("Delete “%1” permanently? This cannot be undone.").arg(deleteDialog.worldName)
        }

        onAccepted: menu.game.deleteWorld(folder)
    }
}
