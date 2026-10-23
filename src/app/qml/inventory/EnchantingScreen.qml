pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The enchanting table's screen (SlotScreen, decision E-864), laid out like the reference's: the item
// slot and the lapis slot on the left, the three offers on the right and the inventory below. An
// offer shows the levels it needs, its price in levels and lapis and the first enchantment it gives;
// a click takes it. The slots are the inventory model's machine rows; the server decides and sends
// them back.
SlotScreen {
    id: screen

    readonly property var enchanting: game.enchanting
    readonly property int itemRow: 46

    panelContent: content

    // An offer as a button reads it (decision E-725): "Level 12: Efficiency II … (3 levels, 3 lapis)".
    function offerText(offer: var): string {
        if (offer.levelCost <= 0)
            return qsTr("No offer")
        return qsTr("Level %1: %2 … (%n level(s), %n lapis)", "", offer.price).arg(offer.levelCost).arg(offer.clue)
    }

    ColumnLayout {
        id: content
        anchors.centerIn: parent
        spacing: Ui.space.m

        Label {
            text: qsTr("Enchanting Table")
            color: Theme.text
            font.pixelSize: Ui.type.title
            font.bold: true
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Ui.space.xl

            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: Ui.space.s

                RowLayout {
                    spacing: Ui.space.s

                    Repeater {
                        model: SlotRange {
                            source: screen.game.player.inventory
                            first: screen.itemRow
                            count: 2
                        }

                        InventorySlot {
                            required property int index
                            objectName: index === 0 ? "enchantingItem" : "enchantingLapis"
                            owner: screen
                        }
                    }
                }

                Label {
                    objectName: "enchantingShelves"
                    text: qsTr("Bookshelves: %1 / 15").arg(screen.enchanting.shelves)
                    color: Theme.textSecondary
                    font.pixelSize: Ui.type.help
                }
            }

            ColumnLayout {
                objectName: "enchantingOffers"
                Layout.alignment: Qt.AlignVCenter
                spacing: Ui.space.s

                Repeater {
                    model: screen.enchanting.offers

                    GameButton {
                        required property var modelData
                        required property int index
                        objectName: "enchantingOffer" + index
                        Layout.preferredWidth: Ui.size.formColumn
                        enabled: modelData.affordable
                        text: screen.offerText(modelData)
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
            text: screen.game.gamepad.active ? qsTr("Put a tool, weapon or armour into the left slot and lapis lazuli beside it, then choose an offer. More bookshelves around the table give higher offers. %1: pick up and put down. %2: put down one. %3: move the stack across. %4 closes.")
                    .arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.west).arg(screen.game.gamepad.pads.texts.north).arg(screen.game.gamepad.pads.texts.east)
                  : qsTr("Put a tool, weapon or armour into the left slot and lapis lazuli beside it, then choose an offer. More bookshelves around the table give higher offers. Left click: pick up and put down. Right click: put down one. Shift-click: move the stack across. %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
            color: Theme.textSecondary
            font.pixelSize: Ui.type.help
        }
    }
}
