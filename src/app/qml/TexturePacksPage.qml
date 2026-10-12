pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Texture packs (decision E-297): every pack found with its preview, resolution, licence and
// problems; switches turn a pack on (it goes on top), arrows change the order of the active ones.
// A change takes effect at once, a running world keeps drawing until the new atlas is ready.
ColumnLayout {
    id: page

    required property GameController game
    readonly property var packs: page.game.settings.texturePacks

    spacing: 8

    Label {

        // Pack texts are data from files: never rich text (E-272).

        textFormat: Text.PlainText
        Layout.fillWidth: true
        text: qsTr("Active packs are used from the top down; a texture a pack does not have comes from the next pack and finally from the game. Own packs go into %1.").arg(page.packs.userDirectory)
        wrapMode: Text.Wrap
        opacity: 0.8
    }

    Label {

        // Pack texts are data from files: never rich text (E-272).

        textFormat: Text.PlainText
        objectName: "texturePackStatus"
        Layout.fillWidth: true
        visible: text.length > 0
        text: page.packs.busy ? qsTr("Building the textures …") : page.packs.status
        wrapMode: Text.Wrap
    }

    Repeater {
        model: page.packs.notices

        Label {

            // Pack texts are data from files: never rich text (E-272).

            textFormat: Text.PlainText
            required property int index

            Layout.fillWidth: true
            text: page.packs.notices[index]
            wrapMode: Text.Wrap
            color: Theme.warning
        }
    }

    ListView {
        id: packList

        objectName: "texturePackList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 200
        clip: true
        spacing: 6
        model: page.packs.packs

        delegate: Frame {
            id: packRow

            required property var modelData
            required property int index

            width: ListView.view.width

            RowLayout {
                anchors.fill: parent
                spacing: 10

                Image {
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 48
                    Layout.alignment: Qt.AlignTop
                    source: packRow.modelData.preview
                    sourceSize: Qt.size(96, 48)
                    fillMode: Image.PreserveAspectFit
                    smooth: false
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {

                        // Pack texts are data from files: never rich text (E-272).

                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: packRow.modelData.name + (packRow.modelData.resolution > 0 ? " " + qsTr("(%1x)").arg(packRow.modelData.resolution) : "")
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Label {
                        // Pack texts are data from files: never rich text (E-272).
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: packRow.modelData.description
                        wrapMode: Text.Wrap
                    }
                    Label {
                        // Pack texts are data from files: never rich text (E-272).
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: (packRow.modelData.license.length > 0 ? qsTr("Licence: %1").arg(packRow.modelData.license) + " · " : "") + (packRow.modelData.origin === "builtin" ? qsTr("Built in") : packRow.modelData.origin === "mod" ? qsTr("From a mod") : qsTr("Own pack"))
                        opacity: 0.7
                        wrapMode: Text.Wrap
                    }
                    Label {
                        // Pack texts are data from files: never rich text (E-272).
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: packRow.modelData.credits.length > 0
                        text: qsTr("Credits: %1").arg(packRow.modelData.credits)
                        opacity: 0.7
                        wrapMode: Text.Wrap
                    }
                    Label {
                        // Pack texts are data from files: never rich text (E-272).
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: packRow.modelData.error.length > 0
                        text: qsTr("Cannot be used: %1").arg(packRow.modelData.error)
                        color: Theme.error
                        wrapMode: Text.Wrap
                    }
                    Label {
                        // Pack texts are data from files: never rich text (E-272).
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: packRow.modelData.problems.length > 0
                        text: packRow.modelData.problems.length > 0 ? qsTr("%n file(s) skipped or changed:", "", packRow.modelData.problems.length) + "\n" + packRow.modelData.problems.join("\n") : ""
                        color: Theme.warning
                        wrapMode: Text.Wrap
                        maximumLineCount: 8
                        elide: Text.ElideRight
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: 2

                    Switch {
                        objectName: "texturePackSwitch_" + packRow.modelData.id
                        enabled: packRow.modelData.error.length === 0
                        checked: packRow.modelData.active
                        onToggled: page.packs.setActive(packRow.modelData.id, checked)
                    }
                    RowLayout {
                        visible: packRow.modelData.active
                        spacing: 2

                        Button {
                            text: "▲"
                            enabled: packRow.index > 0
                            onClicked: page.packs.move(packRow.modelData.id, -1)

                            ThemedToolTip {
                                shown: parent.hovered
                                text: qsTr("Higher priority")
                            }
                        }
                        Button {
                            text: "▼"
                            enabled: packRow.index + 1 < page.packs.packs.length && page.packs.packs[packRow.index + 1].active
                            onClicked: page.packs.move(packRow.modelData.id, 1)

                            ThemedToolTip {
                                shown: parent.hovered
                                text: qsTr("Lower priority")
                            }
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true

        Button {
            text: qsTr("Open texture pack folder")
            onClicked: page.packs.openFolder()
        }
        Button {
            text: qsTr("Reload")
            onClicked: page.packs.refresh()
        }
    }
}
