pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The trading screen of a villager (SlotScreen, decision E-414): its offers on the left (what they
// cost, what they give, how often they can still be traded), its profession and level with the way
// to the next on the right, the selected offer's costs and result in large slots, the inventory
// below. A click on the result trades once, a shift-click as often as the inventory and the uses
// allow; the server decides and sends the offers back.
SlotScreen {
    id: screen

    readonly property var trade: game.trade
    readonly property var chosen: trade.offers.length > 0 ? trade.offers[Math.min(trade.selected, trade.offers.length - 1)] : null
    // What a row shows while the list shrinks to fewer offers.
    readonly property var emptyOffer: ({
            costs: [],
            result: {
                icon: "",
                count: 0,
                label: ""
            },
            uses: 0,
            maxUses: 0,
            soldOut: true,
            affordable: false
        })

    panelContent: content

    // An offer as a screen reader reads it (decision E-725): "1 × Iron Sword for 2 × Emerald".
    function offerText(offer: var): string {
        const stack = item => item.count > 1 ? qsTr("%1 × %2").arg(item.count).arg(item.label) : item.label;
        return qsTr("%1 for %2").arg(stack(offer.result)).arg(offer.costs.map(stack).join(", "));
    }

    component OfferRow: Rectangle {
        id: row

        required property int index
        // The offer of this row; the list's model is the number of offers, so a trade, which
        // replaces the offers, updates the rows in place and keeps the scroll position.
        readonly property var offer: screen.trade.offers[index] ?? screen.emptyOffer

        objectName: "offer" + index
        activeFocusOnTab: true
        Accessible.role: Accessible.ListItem
        Accessible.name: screen.offerText(offer)
        Accessible.description: offer.soldOut ? qsTr("sold out") : qsTr("%1 of %2 trades left").arg(offer.maxUses - offer.uses).arg(offer.maxUses)
        Accessible.selected: screen.trade.selected === index
        implicitWidth: Ui.px(260)
        implicitHeight: Ui.px(40)
        radius: Ui.px(4)
        color: screen.trade.selected === index ? Theme.rowHighlight : (rowArea.containsMouse ? Theme.slotHover : Theme.slot)
        border.color: screen.trade.selected === index ? Theme.selection : Theme.slotBorder
        opacity: offer.soldOut ? 0.5 : 1.0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Ui.px(6)
            anchors.rightMargin: Ui.px(8)
            spacing: Ui.px(4)

            Repeater {
                model: row.offer.costs

                ItemSlot {
                    // A picture of the trade; the row or the result takes the pad's focus.
                    activeFocusOnTab: false
                    required property var modelData

                    implicitWidth: Ui.px(34)
                    implicitHeight: Ui.px(34)
                    slotIcon: modelData.icon
                    slotLabel: modelData.label
                    slotCount: modelData.count
                    slotEmpty: modelData.icon.length === 0
                    enabled: false
                }
            }

            Label {
                text: "▶"
                color: row.offer.affordable && !row.offer.soldOut ? Theme.text : Theme.textMuted
            }

            ItemSlot {
                // A picture of the trade; the row or the result takes the pad's focus.
                activeFocusOnTab: false
                implicitWidth: Ui.px(34)
                implicitHeight: Ui.px(34)
                slotIcon: row.offer.result.icon
                slotLabel: row.offer.result.label
                slotCount: row.offer.result.count
                slotEmpty: row.offer.result.icon.length === 0
                enabled: false
            }

            Item {
                Layout.fillWidth: true
            }

            Label {
                text: row.offer.soldOut ? qsTr("sold out") : (row.offer.maxUses - row.offer.uses) + "/" + row.offer.maxUses
                color: Theme.textSecondary
                font.pixelSize: Ui.px(12)
            }
        }

        MouseArea {
            id: rowArea

            anchors.fill: parent
            hoverEnabled: true
            onClicked: screen.trade.selected = row.index
        }
    }

    RowLayout {
        id: content

        anchors.centerIn: parent
        spacing: Ui.px(20)

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: Ui.px(6)

            Label {
                text: qsTr("Offers")
                color: Theme.heading
                font.pixelSize: Ui.px(18)
                font.bold: true
            }

            ListView {
                objectName: "offers"
                Accessible.name: qsTr("Offers")
                implicitWidth: Ui.px(260)
                implicitHeight: Math.min(contentHeight, Ui.px(9 * 44))
                clip: true
                spacing: Ui.px(4)
                model: screen.trade.offers.length
                delegate: OfferRow {}
                ScrollBar.vertical: ThemedScrollBar {}
            }
        }

        ColumnLayout {
            spacing: Ui.px(12)

            Label {
                objectName: "villagerTitle"
                textFormat: Text.PlainText
                text: qsTr("%1 – %2").arg(screen.trade.profession).arg(screen.trade.levelName)
                color: Theme.text
                font.pixelSize: Ui.px(24)
                font.bold: true
            }

            // The villager's experience on the way to its next level.
            Rectangle {
                objectName: "levelBar"
                Layout.fillWidth: true
                implicitHeight: Ui.px(8)
                radius: Ui.px(3)
                color: Theme.slotBorder

                Rectangle {
                    width: (parent.width - 2) * screen.trade.levelProgress
                    height: parent.height - 2
                    x: 1
                    y: 1
                    radius: Ui.px(2)
                    color: Theme.experience
                }
            }

            RowLayout {
                objectName: "chosenOffer"
                Layout.alignment: Qt.AlignHCenter
                spacing: Ui.px(12)
                visible: screen.chosen !== null

                Repeater {
                    model: screen.chosen ? screen.chosen.costs : []

                    ItemSlot {
                        // A picture of the trade; the row or the result takes the pad's focus.
                        activeFocusOnTab: false
                        required property var modelData

                        slotIcon: modelData.icon
                        slotLabel: modelData.label
                        slotCount: modelData.count
                        slotEmpty: modelData.icon.length === 0
                        enabled: false
                    }
                }

                Label {
                    text: "▶"
                    color: screen.chosen && screen.chosen.affordable && !screen.chosen.soldOut ? Theme.text : Theme.textMuted
                    font.pixelSize: Ui.px(22)
                }

                ItemSlot {
                    objectName: "tradeResult"
                    slotLabel: screen.chosen ? screen.chosen.result.label : ""
                    Accessible.name: screen.chosen ? qsTr("Trade: %1").arg(screen.offerText(screen.chosen)) : qsTr("No offer chosen")
                    slotIcon: screen.chosen ? screen.chosen.result.icon : ""
                    slotCount: screen.chosen ? screen.chosen.result.count : 0
                    slotEmpty: !screen.chosen
                    highlighted: resultArea.containsMouse && screen.chosen && screen.chosen.affordable && !screen.chosen.soldOut
                    opacity: screen.chosen && screen.chosen.affordable && !screen.chosen.soldOut ? 1.0 : 0.5

                    MouseArea {
                        id: resultArea

                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: mouse => screen.trade.trade(screen.trade.selected, (mouse.modifiers & Qt.ShiftModifier) !== 0)
                    }
                }
            }

            PlayerSlots {
                owner: screen
            }

            Label {
                Layout.maximumWidth: Ui.px(9 * 54)
                wrapMode: Text.Wrap
                text: screen.game.gamepad.active ? qsTr("Choose an offer on the left. %1 on the result trades once, %2 trades as often as you can pay. The villager restocks at its workstation. %3 closes.")
                      .arg(screen.game.gamepad.pads.texts.south).arg(screen.game.gamepad.pads.texts.north).arg(screen.game.gamepad.pads.texts.east)
                      : qsTr("Choose an offer on the left. Click the result to trade once, shift-click to trade as often as you can pay. The villager restocks at its workstation. %1 or Esc closes.").arg(screen.game.settings.keys.labels.inventory)
                color: Theme.textSecondary
            }
        }
    }
}
