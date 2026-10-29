// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fixtures/QcPlayerUseFixture.h"
#include "fixtures/QcTestUtil.h"

#include <cstdlib>
#include <utility>

// A player at an enchanting table (decision E-864), used through the protocol: the table with
// bookshelves around it, an iron pickaxe and lapis in its slots, and rejoining after the
// connection ended.
class QcEnchantingFixture : public QcPlayerUseFixture
{
    Q_OBJECT

protected:
    static constexpr int itemSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    static constexpr int lapisSlot = QcMsg::MoveInventoryItem::machineFuelSlot;

    QcItemStack stackOf(const char *name, QList<QcEnchantment> enchantments = {}) const
    {
        return QcItemStack{item(name), 1, 0}.withEnchantments(std::move(enchantments));
    }

    // The table three blocks east of the player.
    QcBlockPos tablePos() const { return m_feet.offset(3, 0, 0); }

    // An enchanting table at tablePos() with `shelves` bookshelves on the ring around it (at its
    // height first, the west side left open), opened through the protocol.
    QcBlockPos openTable(int shelves)
    {
        const QcBlockPos table = tablePos();
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                putAt(table.offset(dx, 0, dz), qcAirBlockId);
                putAt(table.offset(dx, 1, dz), qcAirBlockId);
            }
        }
        putAt(table, id("qtcraft:enchanting_table"));
        int placed = 0;
        for (int dy = 0; dy <= 1 && placed < shelves; ++dy) {
            for (int dz = -2; dz <= 2 && placed < shelves; ++dz) {
                for (int dx = -2; dx <= 2 && placed < shelves; ++dx) {
                    if ((std::abs(dx) == 2 || std::abs(dz) == 2) && !(dx == -2 && dz == 0)) {
                        putAt(table.offset(dx, dy, dz), id("qtcraft:bookshelf"));
                        ++placed;
                    }
                }
            }
        }
        send(QcMsg::UseItem{table, QcBlockFace::West});
        ticks(1);
        return table;
    }

    // The server's inventory slot holding `wanted` (the raw client hears of direct changes only later).
    int serverSlotOf(QcItemId wanted)
    {
        for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
            if (player().inventory.slot(slot).item == wanted && !player().inventory.slot(slot).isEmpty()) {
                return slot;
            }
        }
        return -1;
    }

    // Holds an iron pickaxe and lapis and puts both into the open table.
    void putPickaxeAndLapis(int lapis)
    {
        hold(QStringLiteral("iron_pickaxe"));
        move(slotOf(item("qtcraft:iron_pickaxe")), itemSlot);
        player().inventory.add(QcItemStack{item("qtcraft:lapis_lazuli"), lapis, 0}, *m_content);
        move(serverSlotOf(item("qtcraft:lapis_lazuli")), lapisSlot);
    }

    // Rejoins as the same player after the connection ended.
    void rejoin()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(QStringLiteral("Raw"));
        QcTestUtil::pump();
        ticks(2);
    }
};
