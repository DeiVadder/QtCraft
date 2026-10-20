pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Mods found in the mods directory with their state; switches enable or disable a mod for the next
// world start.
Popup {
    id: screen

    required property GameController game

    anchors.centerIn: parent
    width: Math.min(parent.width - Ui.px(64), Ui.px(760))
    height: Math.min(parent.height - Ui.px(64), Ui.px(560))
    modal: true
    focus: true
    padding: Ui.px(16)

    onOpened: game.library.refreshMods()

    ColumnLayout {
        anchors.fill: parent
        focus: true
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: Ui.px(8)

        Label {
            text: qsTr("Mods")
            font.pixelSize: Ui.px(24)
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            text: screen.game.library.modsDirectory.length > 0
                  ? qsTr("Mods are loaded from %1. Changes take effect when a world is started.").arg(screen.game.library.modsDirectory)
                  : qsTr("Mods are switched off for this session.")
            wrapMode: Text.Wrap
            opacity: 0.8
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Opening a world without a mod it was played with removes that mod's blocks and items from it.")
            wrapMode: Text.Wrap
            color: Theme.warning
        }

        ListView {
            id: modList

            Accessible.name: qsTr("Mods")
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Ui.px(6)
            model: screen.game.library.mods

            Label {
                anchors.centerIn: parent
                visible: modList.count === 0
                text: qsTr("No mods installed. Put a mod folder with a pack.json into the mods directory.")
                wrapMode: Text.Wrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }

            delegate: Frame {
                id: modRow

                required property var modelData

                width: ListView.view.width

                RowLayout {
                    anchors.fill: parent
                    spacing: Ui.px(12)

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Ui.px(2)

                        Label {
                            textFormat: Text.PlainText
                            text: modRow.modelData.version.length > 0
                                  ? qsTr("%1 %2").arg(modRow.modelData.name).arg(modRow.modelData.version)
                                  : modRow.modelData.name
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            // Written by the mod's author: never rich text (E-272).
                            textFormat: Text.PlainText
                            text: modRow.modelData.description
                            wrapMode: Text.Wrap
                        }
                        Label {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: modRow.modelData.state === "loaded" ? qsTr("Active")
                                : modRow.modelData.state === "disabled" ? qsTr("Disabled")
                                : qsTr("Not loaded: %1").arg(modRow.modelData.reason)
                            color: modRow.modelData.state === "failed" ? Theme.error : palette.text
                            wrapMode: Text.Wrap
                        }
                    }

                    Switch {
                        Accessible.name: qsTr("Enable %1").arg(modRow.modelData.name)
                        visible: modRow.modelData.id.length > 0
                        checked: modRow.modelData.state !== "disabled"
                        onToggled: screen.game.library.setModEnabled(modRow.modelData.id, checked)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: qsTr("Open mods folder")
                enabled: screen.game.library.modsDirectory.length > 0
                onClicked: screen.game.library.openModsFolder()
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                text: qsTr("Close")
                onClicked: screen.close()
            }
        }
    }
}
