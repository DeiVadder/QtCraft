// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcBoatMotion.h"
#include "common/world/QcBoatBody.h"
#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/boats/QcServerBoats.h"
#include "server/entities/QcEntityChecks.h"

#include <QTest>

#include <algorithm>

// Boats on a test server (decision E-920): a pond east of the player, boats placed on it by the
// player or spawned directly, creatures to put in them, and the driver's inputs.
class QcBoatFixture : public QcServerTestFixture
{
    Q_OBJECT

protected:
    // The pond east of the player: water from 2 to 14 blocks east and 3 blocks to either side, two
    // deep below the player's ground level; its surface lies at the level of his feet.
    void digPond()
    {
        // A flat bank of grass around it.
        for (int dx = 1; dx <= 16; ++dx) {
            for (int dz = -5; dz <= 5; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -2, dz), id("qtcraft:stone"));
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:grass_block"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
        for (int dx = 2; dx <= 14; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -3, dz), id("qtcraft:stone"));
                m_server->world().setBlock(m_feet.offset(dx, -2, dz), id("qtcraft:water"));
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:water"));
                for (int dy = 0; dy <= 3; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    const QcBoat &boat(quint32 boatId) const { return *m_server->entities().boats().find(boatId); }

    double floatingHeight() const { return m_feet.y - QcBoatMotion::liquidSurfaceDrop - QcBoatMotion::draft; }

    void input(float forward, float strafe, bool sneak = false)
    {
        QcPlayerInput in;
        in.sequence = ++m_sequence;
        in.forward = forward;
        in.strafe = strafe;
        in.sneak = sneak;
        in.yaw = 90.0f;
        m_client->connection.send(QcMsg::PlayerInput{in});
        QcTestUtil::pump();
        ticks(1);
    }

    QStringList entityProblems() const { return QcEntityChecks::problems(m_server->entities()); }

    quint32 spawnCreature(const QString &name, const QcVec3 &at)
    {
        const auto it = std::ranges::find(m_content->mobs(), QStringLiteral("qtcraft:") + name, &QcMobDefinition::name);
        if (it == m_content->mobs().end()) {
            QcTestUtil::failCase(QStringLiteral("unknown creature %1").arg(name));
            return 0;
        }
        const int type = static_cast<int>(it - m_content->mobs().begin());
        return m_server->entities().creatures().spawn(type, at, m_server->entities().newId(), false, true);
    }

    QcBoat *spawnBoat(const QcVec3 &at, float yaw)
    {
        QcBoat boat;
        boat.item = QStringLiteral("qtcraft:oak_boat");
        boat.motion.position = at;
        boat.yaw = yaw;
        return m_server->systems().boats.spawn(boat);
    }

    // Joins, digs the pond and places a boat on it as a player does, looking east; its id (0 when
    // none was placed).
    quint32 placeBoatOnPond()
    {
        join();
        chat(QStringLiteral("/gamerule mobSpawning false"));
        digPond();
        chat(QStringLiteral("/give oak_boat"));
        ticks(2);

        // Looking east and using it on the water, the boat floats on its surface pointing east.
        input(0.0f, 0.0f);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:oak_boat"))});
        send(QcMsg::UseItem{m_feet.offset(3, -1, 0), QcBlockFace::Up});
        const QList<QcBoat> &boats = m_server->entities().boats().all();
        return boats.isEmpty() ? 0 : boats.constFirst().id;
    }

};
