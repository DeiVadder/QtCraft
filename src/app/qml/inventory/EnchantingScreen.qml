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
// are readable; only the runes and the clue's letters use the pixel font (decision of stage 76.1,
// IV-3). The slots are the inventory model's machine rows; the server decides and sends them back.
SlotScreen {
    id: screen

    readonly property var enchanting: game.enchanting
    readonly property int itemRow: 46

    title: qsTr("Enchanting Table")
    handbookTarget: "qtcraft:enchanting_table"

    RowLayout {
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

            // The slots' names below them (IV-8 of docs/Menue-Pruefung.md).
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Item · Lapis")
                color: Theme.textMuted
                font.pixelSize: Ui.type.help
            }

            Row {
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
                    font.pixelSize: Ui.type.body
                    Accessible.ignored: true
                }
            }
        }

        ColumnLayout {
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
}
