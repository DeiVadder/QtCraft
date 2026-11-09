// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fixtures/QcServerTestFixture.h"
#include "server/core/QcServerWorld.h"

using namespace Qt::StringLiterals;

// What the creature tests of the nether share: a floor of one block under the feet and a world on
// disk that is opened again. Ticks are stepped by hand.
class QcMobStageFixture : public QcServerTestFixture
{
    Q_OBJECT

protected:
    QcCreatureKindId kind(const QString &name) const { return *m_content->mobKind(u"qtcraft:"_s + name); }
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    void joinStage(const QString &mode = u"survival"_s)
    {
        join();
        loadAround(m_feet, 2);
        chat(u"/gamemode "_s + mode);
        m_server->mobs().setNaturalSpawning(false);
        m_server->mobs().removeAll();
    }

    quint32 spawn(const QString &name, double dx, double dz, double dy = 0.0)
    {
        const QcVec3 at{m_feet.x + dx + 0.5, m_feet.y + dy, m_feet.z + dz + 0.5};
        return m_server->mobs().spawn(kind(name), at, m_server->entities().newId());
    }

    void hold(const char *name)
    {
        chat(u"/give "_s + QString::fromLatin1(name).section(u':', -1));
        send(QcMsg::SelectHotbarSlot{slotOf(item(name))});
        ticks(1);
    }

    // A floor of `block` below the feet level, `size` blocks to every side, with air above it up
    // to `height`.
    void floorOf(const char *block, int size, int height = 4)
    {
        for (int dx = -size; dx <= size; ++dx) {
            for (int dz = -size; dz <= size; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id(block));
                for (int dy = 0; dy <= height; ++dy) {
                    m_server->world().setBlock(m_feet.offset(dx, dy, dz), qcAirBlockId);
                }
            }
        }
    }

    static QcServerConfig configIn(const QString &directory)
    {
        QcServerConfig config;
        config.worldName = u"Stage"_s;
        config.seed = 4711;
        config.worldDirectory = directory;
        return config;
    }

    // Opens the world of `directory` again and joins it as the same player.
    void reopen(const QString &directory)
    {
        m_client.reset();
        m_server.reset();
        QcServerConfig config = configIn(directory);
        config.viewDistance = m_viewDistance;
        config.keepChunksLoaded = true;
        config.operators = {u"Raw"_s};
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
        ticks(1);
    }
};
