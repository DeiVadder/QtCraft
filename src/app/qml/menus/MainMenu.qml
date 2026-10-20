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
        anchors.margins: Ui.px(32)
        spacing: Ui.px(16)

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("QtCraft")
            color: Theme.text
            // Smaller in a low window, so the lists and the form keep their room.
            font.pixelSize: Ui.px(menu.height < Ui.px(760) ? 48 : 64)
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: Ui.px(900)
            Layout.alignment: Qt.AlignHCenter
            spacing: Ui.px(24)

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: Ui.px(540)
                spacing: Ui.px(8)

                Label {
                    text: qsTr("Your worlds")
                    color: Theme.text
                    font.pixelSize: Ui.px(20)
                }

                ListView {
                    id: worldList

                    Accessible.name: qsTr("Your worlds")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Ui.px(4)
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
                        height: Ui.px(64)
                        radius: Ui.px(4)
                        color: Theme.scrim

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: Ui.px(8)
                            spacing: Ui.px(8)

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Ui.px(2)

                                Label {
                                    textFormat: Text.PlainText
                                    text: worldRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: Ui.px(17)
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }

                                Label {
                                    textFormat: Text.PlainText
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

            // The new world's form scrolls when the window is too low for it; creating the world
            // and the menu's other actions stay pinned below it.
            ColumnLayout {
                Layout.fillWidth: false
                Layout.fillHeight: true
                Layout.preferredWidth: Ui.px(320)
                Layout.maximumWidth: Ui.px(320)
                spacing: Ui.px(8)

                Label {
                    text: qsTr("New world")
                    color: Theme.text
                    font.pixelSize: Ui.px(20)
                }

                Flickable {
                    id: newWorldForm

                    objectName: "newWorldForm"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: contentHeight
                    Layout.maximumHeight: contentHeight
                    Layout.minimumHeight: Ui.px(64)
                    clip: true
                    contentWidth: width
                    contentHeight: formFields.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ThemedScrollBar {}

                    ColumnLayout {
                        id: formFields

                        // The column keeps a gutter on the right for the scroll bar.
                        width: newWorldForm.width - Ui.px(14)
                        spacing: Ui.px(8)

                        Label {
                            text: qsTr("World name")
                            color: Theme.text
                        }

                        TextField {
                            id: worldName

                            Accessible.name: qsTr("World name")
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
                            Accessible.name: qsTr("World type")
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

                            Accessible.name: qsTr("Seed (number or text, empty for random)")
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

                            Accessible.name: qsTr("Layers from the bottom up (empty for the default)")
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
                            Accessible.name: qsTr("Game mode")
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
                    }
                }

                Button {
                    objectName: "createWorldButton"
                    Layout.fillWidth: true
                    Layout.rightMargin: Ui.px(14)
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

                GridLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Ui.px(8)
                    Layout.rightMargin: Ui.px(14)
                    columns: 2
                    columnSpacing: Ui.px(8)
                    rowSpacing: Ui.px(8)

                    Button {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: qsTr("Multiplayer")
                        onClicked: multiplayerScreen.open()
                    }

                    Button {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: qsTr("Settings")
                        onClicked: settingsScreen.open()
                    }

                    Button {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: qsTr("Mods")
                        onClicked: modsScreen.open()
                    }

                    Button {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: qsTr("Handbook (%1)").arg(menu.game.settings.keys.labels.handbook)
                        onClicked: menu.game.handbook.open("")
                    }

                    Button {
                        objectName: "quitButton"
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        text: qsTr("Quit")
                        onClicked: menu.quitRequested()
                    }
                }

                // Keeps the column at the top when the window has room to spare.
                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    SettingsScreen {
        id: settingsScreen

        objectName: "menuSettingsScreen"
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

        objectName: "menuCharacterScreen"
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
        // A fixed width: the style's implicit width would follow the scaled font in a loop.
        width: Math.min(parent.width - Ui.px(64), Ui.px(460))
        modal: true
        title: qsTr("Delete world")
        standardButtons: Dialog.Yes | Dialog.Cancel

        Label {
            width: deleteDialog.availableWidth
            wrapMode: Text.Wrap
            text: qsTr("Delete “%1” permanently? This cannot be undone.").arg(deleteDialog.worldName)
        }

        onAccepted: menu.game.library.deleteWorld(folder)
        onClosed: menu.subScreenClosed()
    }
}
