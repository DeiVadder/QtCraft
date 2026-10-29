// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The enchanting table's screen (SlotScreen, decisions E-864 and E-884), laid out like the
// reference's: the open book above the item slot and the lapis slot on the left, the bookshelves that
// count below them, the three offers as fields of parchment on the right (EnchantingOffer: lapis,
// clue in runes, levels) and the inventory below. The more bookshelves, the more letters of a clue
// are readable. The slots are the inventory model's machine rows; the server decides and sends them
// back.
SlotScreen {
    id: screen

    readonly property var enchanting: game.enchanting
    readonly property int itemRow: 46

    panelContent: content

    ColumnLayout {
        id: content
        anchors.centerIn: parent
        spacing: Ui.space.m

        WorkbenchTitle {
            text: qsTr("Enchanting Table")
            item: "qtcraft:enchanting_table"
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Ui.space.xl

            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: Ui.space.s

                PixelArt {
                    Layout.alignment: Qt.AlignHCenter
                    picture: "book"
                    pixel: Ui.size.artPixel * 2
                }

                MachineSlotRow {
                    Layout.alignment: Qt.AlignHCenter
                    owner: screen
                    first: screen.itemRow
                    names: ["enchantingItem", "enchantingLapis"]
                    placeholders: ["", "qtcraft:lapis_lazuli"]
                }

                Row {
                    objectName: "enchantingShelves"
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Ui.space.xs
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Bookshelves: %1 / 15").arg(screen.enchanting.shelves)

                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Ui.size.icon
                        height: Ui.size.icon
                        source: ItemIcons.source("qtcraft:bookshelf", ItemIcons.generation)
                        smooth: false
                        Accessible.ignored: true
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: screen.enchanting.shelves + " / 15"
                        color: Theme.textSecondary
                        font.family: Ui.pixelFont.name
                        font.pixelSize: Ui.type.section
                        Accessible.ignored: true
                    }
                }
            }

            ColumnLayout {
                objectName: "enchantingOffers"
                Layout.alignment: Qt.AlignVCenter
                spacing: Ui.space.s

                Repeater {
                    model: screen.enchanting.offers

                    EnchantingOffer {
                        required property var modelData
                        required property int index
                        objectName: "enchantingOffer" + index
                        offerData: modelData
                        onClicked: screen.enchanting.choose(index)
                    }
                }
            }
        }

        PlayerSlots {
            owner: screen
        }

        Label {
            Layout.maximumWidth: Ui.size.formColumn * 2
            wrapMode: Text.Wrap
            text: screen.game.gamepad.active ? qsTr("Put a tool, weapon or armour into the left slot and lapis lazuli beside it, then choose an offer. More bookshelves around the table give higher offers and make more of their runes readable. %1: pick up and put down. %2: put down one. %3: move the stack across. %4 closes.")
                    .arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.west).arg(screen.game.gamepad.pads.texts.north).arg(screen.game.gamepad.pads.texts.east)
                  : qsTr("Put a tool, weapon or armour into the left slot and lapis lazuli beside it, then choose an offer. More bookshelves around the table give higher offers and make more of their runes readable. Left click: pick up and put down. Right click: put down one. Shift-click: move the stack across. %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
            color: Theme.textSecondary
            font.pixelSize: Ui.type.help
        }
    }
}
