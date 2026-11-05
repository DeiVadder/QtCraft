// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Texture packs (decision E-297): every pack found with its preview, resolution, licence and
// problems, the active ones on top in their order and the others below; switches turn a pack on (it
// goes on top), arrows change the order of the active ones.
// A change takes effect at once, a running world keeps drawing until the new atlas is ready. Pack
// texts are data from files: never rich text (E-272).
SettingsPage {
    id: page

    required property GameController game
    readonly property var packs: page.game.settings.texturePacks

    title: qsTr("Texture packs")

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Ui.space.s

        Hint {
            Layout.fillWidth: true
            text: qsTr("Active packs are used from the top down; a texture a pack does not have comes from the next pack and finally from the game.")
        }

        Hint {
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.packs.busy ? qsTr("Building the textures …") : page.packs.status
        }

        Repeater {
            model: page.packs.notices

            Hint {
                required property int index

                Layout.fillWidth: true
                kind: Hint.Warning
                text: page.packs.notices[index]
            }
        }
    }

    // The active packs on top in their order, the others below (TP-3 of docs/Menue-Pruefung.md).
    Repeater {
        model: [
            { name: "texturePackList", title: qsTr("Active"), active: true },
            { name: "inactiveTexturePacks", title: qsTr("Not active"), active: false }
        ]

        ColumnLayout {
            id: group

            required property var modelData
            readonly property var listed: modelData.active ? page.packs.activePacks : page.packs.inactivePacks

            objectName: modelData.name
            Layout.fillWidth: true
            visible: listed.length > 0
            spacing: Ui.space.s

            Label {
                text: group.modelData.title
                font.pixelSize: Ui.type.group
                font.bold: true
                color: Theme.heading
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            Repeater {
                model: group.listed

                delegate: TexturePackRow {
                    packs: page.packs
                    activeCount: group.modelData.active ? group.listed.length : 0
                }
            }
        }
    }

    // The page's actions below the list, on the left (TP-1 of docs/Menue-Pruefung.md).
    RowLayout {
        spacing: Ui.space.s

        GameButton {
            text: qsTr("Open texture pack folder")
            onClicked: page.packs.openFolder()

            ThemedToolTip {
                shown: parent.hovered
                text: page.packs.userDirectory
            }
        }
        GameButton {
            text: qsTr("Reload")
            onClicked: page.packs.refresh()
        }
    }
}
