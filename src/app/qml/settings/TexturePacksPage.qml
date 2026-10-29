// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// Texture packs (decision E-297): every pack found with its preview, resolution, licence and
// problems; switches turn a pack on (it goes on top), arrows change the order of the active ones.
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
            objectName: "texturePackStatus"
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

    ColumnLayout {
        objectName: "texturePackList"
        Layout.fillWidth: true
        spacing: Ui.space.s

        Repeater {
            model: page.packs.packs

            delegate: TexturePackRow {
                packs: page.packs
            }
        }
    }
}
