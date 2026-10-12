pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

Rectangle {
    id: menu

    required property GameController game

    signal quitRequested

    objectName: "mainMenu"
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.menuTop }
        GradientStop { position: 1.0; color: Theme.menuBottom }
    }

    focus: visible
    onVisibleChanged: {
        if (visible) {
            game.library.refreshWorlds();
            forceActiveFocus();
        } else {
            // Its sub-screens belong to the menu and close with it.
            settingsScreen.close();
            modsScreen.close();
            characterScreen.close();
            multiplayerScreen.close();
            deleteDialog.close();
        }
    }
    // Keys the text fields and buttons leave; Tab moves the focus.
    Keys.onPressed: event => {
        if (menu.game.settings.keys.matches("handbook", event.key) && !menu.game.handbook.isOpen)
            menu.game.handbook.open("");
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    // Opens the multiplayer screen; with an address it joins from there (--overlay multiplayer).
    function openMultiplayer(address: string, name: string, password: string) {
        multiplayerScreen.open();
        if (address.length > 0)
            multiplayerScreen.joinServer(address, name, password);
    }

    function subScreenClosed() {
        if (visible)
            forceActiveFocus();
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 16

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("QtCraft")
            color: Theme.text
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
                    color: Theme.text
                    font.pixelSize: 20
                }

                ListView {
                    id: worldList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: menu.game.library.worlds

                    Label {
                        anchors.centerIn: parent
                        visible: worldList.count === 0
                        text: qsTr("No saved worlds yet.")
                        color: Theme.textSecondary
                    }

                    delegate: Rectangle {
                        id: worldRow

                        required property var modelData

                        width: ListView.view.width
                        height: 64
                        radius: 4
                        color: Theme.scrim

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    textFormat: Text.PlainText
                                    text: worldRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: 17
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Label {
                                    text: worldRow.modelData.error.length > 0
                                          ? worldRow.modelData.error
                                          : worldRow.modelData.generator === "showcase"
                                            ? qsTr("Last played %1 · Showcase").arg(worldRow.modelData.lastPlayed)
                                          : worldRow.modelData.generator === "flat"
                                            ? qsTr("Last played %1 · Flat").arg(worldRow.modelData.lastPlayed)
                                          : worldRow.modelData.generator === "void"
                                            ? qsTr("Last played %1 · Void").arg(worldRow.modelData.lastPlayed)
                                            : qsTr("Last played %1 · Seed %2").arg(worldRow.modelData.lastPlayed).arg(worldRow.modelData.seed)
                                    color: worldRow.modelData.error.length > 0 ? Theme.error : Theme.textSecondary
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

                ProfileCard {
                    Layout.fillWidth: true
                    game: menu.game
                    covered: settingsScreen.visible || modsScreen.visible || characterScreen.visible || deleteDialog.visible
                             || multiplayerScreen.visible || menu.game.handbook.isOpen
                    onCustomizeRequested: characterScreen.open()
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
                    color: Theme.text
                    font.pixelSize: 20
                }

                Label {
                    text: qsTr("World name")
                    color: Theme.text
                }

                TextField {
                    id: worldName

                    Layout.fillWidth: true
                    text: qsTr("New World")
                    selectByMouse: true
                }

                Label {
                    text: qsTr("World type")
                    color: Theme.text
                }

                ComboBox {
                    id: worldType

                    objectName: "worldTypeChoice"
                    Layout.fillWidth: true
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { value: "terrain", text: qsTr("Normal") },
                        { value: "flat", text: qsTr("Flat") },
                        { value: "void", text: qsTr("Void") },
                        { value: "showcase", text: qsTr("Showcase") }
                    ]
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    text: worldType.currentValue === "flat" ? qsTr("The same layers everywhere, no caves or trees, no creatures of their own accord.")
                          : worldType.currentValue === "void" ? qsTr("Nothing but a small platform; noon, no creatures of their own accord.")
                          : worldType.currentValue === "showcase" ? qsTr("A flat world with every block, creature and item on show, at noon.")
                          : qsTr("Hills, mountains, caves, oceans and biomes.")
                }

                Label {
                    visible: worldType.currentValue === "terrain"
                    text: qsTr("Seed (number or text, empty for random)")
                    color: Theme.text
                }

                TextField {
                    id: seed

                    visible: worldType.currentValue === "terrain"
                    Layout.fillWidth: true
                    selectByMouse: true
                }

                Label {
                    visible: worldType.currentValue === "flat"
                    text: qsTr("Layers from the bottom up (empty for the default)")
                    color: Theme.text
                }

                TextField {
                    id: flatLayers

                    visible: worldType.currentValue === "flat"
                    Layout.fillWidth: true
                    selectByMouse: true
                    placeholderText: visible ? menu.game.defaultFlatLayers() : ""
                }

                Label {
                    text: qsTr("Game mode")
                    color: Theme.text
                }

                ComboBox {
                    id: gameMode

                    objectName: "gameModeChoice"
                    Layout.fillWidth: true
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { value: "survival", text: qsTr("Survival") },
                        { value: "creative", text: qsTr("Creative") }
                    ]
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondary
                    text: gameMode.currentValue === "creative"
                          ? qsTr("Every block and item without limit, flying, instant breaking, no damage or hunger.")
                          : qsTr("Gather, craft and survive: health, hunger and creatures count.")
                }

                CheckBox {
                    id: keepInventory

                    Layout.fillWidth: true
                    checked: true
                    text: qsTr("Keep inventory on death")
                    palette.windowText: Theme.text
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Create and play")
                    highlighted: true
                    onClicked: menu.game.createWorld(worldName.text,
                                                     seed.text.length > 0 ? seed.text : String(Math.floor(Math.random() * 2147483647)), {
                                                         keepInventory: keepInventory.checked,
                                                         worldType: worldType.currentValue,
                                                         layers: worldType.currentValue === "flat" ? flatLayers.text : "",
                                                         gameMode: gameMode.currentValue
                                                     })
                }

                Item {
                    Layout.preferredHeight: 24
                }

                Button {
                    Layout.fillWidth: true
                    text: qsTr("Multiplayer")
                    onClicked: multiplayerScreen.open()
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
                    text: qsTr("Handbook (%1)").arg(menu.game.settings.keys.labels.handbook)
                    onClicked: menu.game.handbook.open("")
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
        onClosed: menu.subScreenClosed()
    }

    ModsScreen {
        id: modsScreen

        objectName: "modsScreen"
        game: menu.game
        onClosed: menu.subScreenClosed()
    }

    CharacterScreen {
        id: characterScreen

        game: menu.game
        onClosed: menu.subScreenClosed()
    }

    MultiplayerScreen {
        id: multiplayerScreen

        game: menu.game
        onClosed: menu.subScreenClosed()
    }

    // A join started elsewhere ("join again", --join) shows its progress here.
    Connections {
        target: menu.game.multiplayer

        function onJoiningChanged() {
            if (menu.visible && menu.game.multiplayer.joining && !multiplayerScreen.visible)
                multiplayerScreen.open();
        }
    }

    Dialog {
        id: deleteDialog

        objectName: "deleteWorldDialog"
        property string folder
        property string worldName

        anchors.centerIn: parent
        modal: true
        title: qsTr("Delete world")
        standardButtons: Dialog.Yes | Dialog.Cancel

        Label {
            text: qsTr("Delete “%1” permanently? This cannot be undone.").arg(deleteDialog.worldName)
        }

        onAccepted: menu.game.library.deleteWorld(folder)
        onClosed: menu.subScreenClosed()
    }
}
