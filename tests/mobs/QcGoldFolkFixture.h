// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcMining.h"
#include "mobs/QcMobStageFixture.h"

#include <QHash>

using namespace Qt::StringLiterals;

// A stage for the tuskling and the bristleboar (decisions E-1081 to E-1083): a floor of grass, the
// creatures on it and the items lying around. Ticks are stepped by hand.
class QcGoldFolkFixture : public QcMobStageFixture
{
    Q_OBJECT

protected:
    const QcCreature &mob(quint32 id) { return *m_server->mobs().find(id); }

    void joinPasture(const QString &mode = u"survival"_s)
    {
        joinStage(mode);
        floorOf("qtcraft:grass_block", 10);
    }

    void wearGold() { player().equipment.stacks[0] = QcItemStack{item("qtcraft:golden_helmet"), 1, 0}; }

    // A wall of stone between the player and everything east of x + 2, two blocks high.
    void wallEast()
    {
        for (int dz = -4; dz <= 4; ++dz) {
            for (int dy = 0; dy <= 3; ++dy) {
                m_server->world().setBlock(m_feet.offset(2, dy, dz), id("qtcraft:stone"));
            }
        }
    }

    // Lays `stack` on the floor for anybody to take at once; returns its entity id.
    quint32 lay(const QcItemStack &stack, double dx, double dz)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, m_feet.y + 0.1, m_feet.z + dz + 0.5};
        const quint32 entity = m_server->entities().newId();
        m_server->entities().items().spawn({.stack = stack, .position = at, .velocity = {}, .pickupDelay = 0.0}, entity);
        return entity;
    }

    // The items lying around, by name, summed.
    QHash<QString, int> lyingAround() const
    {
        QHash<QString, int> counts;
        for (const QcItemEntity &item : m_server->entities().items().items()) {
            counts[m_content->item(item.stack.item).name] += item.stack.count;
        }
        return counts;
    }

    void mine(QcBlockPos pos)
    {
        send(QcMsg::StartMining{pos});
        const QcBlockDefinition &block = m_content->block(*m_server->world().blockAt(pos));
        ticks(static_cast<int>(std::ceil(QcMining::breakSeconds(block, &m_content->item(item("qtcraft:iron_pickaxe"))) * QcPhysics::tickRate)) + 1);
        send(QcMsg::FinishMining{pos});
        ticks(1);
    }
};
