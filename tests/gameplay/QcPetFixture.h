// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcDayCycle.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/animals/QcPetCare.h"
#include "server/animals/QcPetOwners.h"

#include <cmath>

// The common ground of the taming tests (decision E-480): a flat, open arena of grass around the
// player by day, creatures spawned in it, items in the player's hand, uses of a creature through the
// game and content with other taming chances. Ticks are stepped by hand.
class QcPetFixture : public QcServerTestFixture
{
    Q_OBJECT

protected:
    static constexpr double noon = QcDayCycle::dayLength * 10.25;
    static constexpr double midnight = QcDayCycle::dayLength * 10.75;
    static constexpr int arena = 30;

    QcServerClient &player() { return *m_server->entities().clients().front(); }
    int type(const char *name) const { return *m_content->mobIndex(QStringLiteral("qtcraft:") + QString::fromLatin1(name)); }
    const QcMobDefinition &kind(const char *name) const { return m_content->mobs()[type(name)]; }
    QcCreature *mob(quint32 id) { return m_server->entities().creatures().find(id); }
    QString ownerKey() { return QcPetOwners::keyOf(player()); }

    // A server with a flat, open arena of grass around the player at `time`, without natural spawns.
    void joinArena(double time = noon)
    {
        join();
        m_server->setWorldTime(time);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
        loadAround(m_feet, 3);
        for (int dx = -arena; dx <= arena; ++dx) {
            for (int dz = -arena; dz <= arena; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 6; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    QcVec3 at(double dx, double dz) const { return {m_feet.x + dx + 0.5, static_cast<double>(m_feet.y), m_feet.z + dz + 0.5}; }

    quint32 spawn(const char *name, double dx, double dz)
    {
        return m_server->entities().creatures().spawn(type(name), at(dx, dz), m_server->entities().newId());
    }

    // A pet of the player at (dx, dz), standing (not sitting) unless `sits`.
    quint32 pet(const char *name, double dx, double dz, bool sits = false)
    {
        const quint32 id = spawn(name, dx, dz);
        QcPetCare::tame(m_server->entities(), *m_content, *mob(id), ownerKey());
        mob(id)->tame->sitting = sits;
        mob(id)->pose = sits ? QcPose::Sitting : QcPose::Standing;
        return id;
    }

    // Puts `count` of the item into the player's hand (nothing for an empty name).
    void hold(const char *name, int count = 64)
    {
        player().inventory.setSlot(player().inventory.selectedHotbarSlot(), name ? QcItemStack{item(name), count, 0} : QcItemStack{});
    }
    int heldCount() { return player().inventory.slot(player().inventory.selectedHotbarSlot()).count; }

    // Uses the held item on the creature through the game, from 2 m in front of it.
    void use(quint32 creature)
    {
        player().motion.position = mob(creature)->motion.position + QcVec3{0.0, 0.0, 2.0};
        player().yaw = 0.0f;
        player().pitch = 20.0f;
        send(QcMsg::UseEntity{creature});
        ticks(1);
    }

    // The base content with the taming chance of `name` set (a mod).
    void setTameChance(const char *name, double chance)
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition changed = kind(name);
        changed.tameable->chance = chance;
        content->registerMob(changed);
        m_content = content;
    }

    double distance(quint32 creature) { return (mob(creature)->motion.position - player().motion.position).length(); }
};
