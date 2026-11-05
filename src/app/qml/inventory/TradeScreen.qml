// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The trading screen of a villager (SlotScreen, decision E-414): its profession and level as the
// title, its offers on the left (what they cost, what they give, how often they can still be
// traded), the way to its next level and the selected offer's costs and result in large slots
// above the inventory. A click on the result trades once, a shift-click as often as the inventory and the uses
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

    title: qsTr("%1 – %2").arg(screen.trade.profession).arg(screen.trade.levelName)
    handbookTarget: screen.trade.handbookTarget
    keyHints: [
        { keys: [qsTr("Click")], text: qsTr("on the result: trade once") },
        { keys: [qsTr("Shift-click")], text: qsTr("as often as you can pay") }
    ]

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
        implicitWidth: Ui.size.recipes
        implicitHeight: Ui.size.offerRow
        radius: Ui.radius
        color: screen.trade.selected === index ? Theme.rowHighlight : (rowArea.containsMouse ? Theme.slotHover : Theme.slot)
        border.color: screen.trade.selected === index ? Theme.selection : Theme.slotBorder
        opacity: offer.soldOut ? 0.5 : 1.0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Ui.space.s
            anchors.rightMargin: Ui.space.s
            spacing: Ui.space.xs

            Repeater {
                model: row.offer.costs

                ItemSlot {
                    // A picture of the trade; the row or the result takes the pad's focus.
                    activeFocusOnTab: false
                    required property var modelData

                    implicitWidth: Ui.size.offerSlot
                    implicitHeight: Ui.size.offerSlot
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
                implicitWidth: Ui.size.offerSlot
                implicitHeight: Ui.size.offerSlot
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
                font.pixelSize: Ui.type.help
            }
        }

        MouseArea {
            id: rowArea

            anchors.fill: parent
            hoverEnabled: true
            onClicked: screen.trade.selected = row.index
        }
    }

    startColumn: ColumnLayout {
        spacing: Ui.space.s

        Label {
            text: qsTr("Offers")
            color: Theme.heading
            font.pixelSize: Ui.type.group
            font.bold: true
        }

        ListView {
            objectName: "offers"
            Accessible.name: qsTr("Offers")
            implicitWidth: Ui.size.recipes
            Layout.fillHeight: true
            Layout.minimumHeight: Ui.size.listMinimum
            clip: true
            spacing: Ui.space.xs
            model: screen.trade.offers.length
            delegate: OfferRow {}
            ScrollBar.vertical: ThemedScrollBar {}
        }
    }

    // The villager's experience on the way to its next level (IV-8 of docs/Menue-Pruefung.md).
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Ui.space.xs

        Label {
            text: qsTr("Villager's experience")
            color: Theme.textMuted
            font.pixelSize: Ui.type.help
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Ui.space.s
            radius: Ui.radius
            color: Theme.slotBorder

            Rectangle {
                // One pixel in from each side.
                width: (parent.width - 2 * x) * screen.trade.levelProgress
                height: parent.height - 2 * y
                x: Ui.size.line
                y: Ui.size.line
                radius: Ui.radius
                color: Theme.experience
            }
        }
    }

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: Ui.space.m
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

        PixelArt {
            Layout.alignment: Qt.AlignVCenter
            picture: screen.chosen && screen.chosen.affordable && !screen.chosen.soldOut ? "arrow/text" : "arrow/textSecondary"
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
}
