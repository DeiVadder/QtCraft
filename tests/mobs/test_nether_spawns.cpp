// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcDayCycle.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/spawning/QcMonsterCap.h"
#include "worldgen/nether/QcNetherGenerator.h"

#include <QElapsedTimer>
#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;

// Who comes by itself under the roof of the nether (decisions E-1050, E-1051, E-1038): only kinds
// that name its biomes, on floors below the roof, never on bedrock, the same at every time of day;
// lava waders on the lava. Ticks are stepped by hand on a real nether, no server.
class TestNetherSpawns : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    quint32 m_nextId = 1;
    static constexpr int radius = 5;
    static constexpr int seconds = 90;
    static constexpr qsizetype maxRottusksNear = 16;

    const QcDimensionDefinition &nether() const { return *m_content->worldgen().dimension(u"qtcraft:nether"_s); }

    std::unique_ptr<QcServerWorld> makeWorld(qint64 seed = 4711, int chunks = radius) const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcNetherGenerator>(m_content, seed));
        for (int x = -chunks; x <= chunks; ++x) {
            for (int z = -chunks; z <= chunks; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    // The kinds that came in `seconds` of sprinted ticks at the world time `time`, with where they
    // stood when they came.
    QHash<QString, QList<QcVec3>> census(QcServerWorld &world, double time)
    {
        QcMobManager mobs(m_content, 9);
        mobs.setDimension(nether());
        const QList<QcMobManager::PlayerView> players{{.entityId = 900000, .position = {8.5, 64.0, 8.5}}};
        QHash<QString, QList<QcVec3>> seen;
        QSet<quint32> counted;
        const double dt = QcPhysics::tickDuration;
        for (int tick = 0; tick < seconds * QcPhysics::tickRate; ++tick) {
            mobs.tick(world, players, time, dt, [this] { return m_nextId++; });
            for (const QcCreature &mob : mobs.mobs()) {
                if (!counted.contains(mob.id)) {
                    counted.insert(mob.id);
                    seen[m_content->mobs()[mob.type].name].append(mob.motion.position);
                }
            }
        }
        return seen;
    }

private slots:
    void onlyKindsThatNameItsBiomesCome()
    {
        const std::unique_ptr<QcServerWorld> world = makeWorld();
        const QHash<QString, QList<QcVec3>> seen = census(*world, QcDayCycle::dayLength * 0.25);
        qInfo() << "came:" << seen.keys();
        const QSet<QString> nativeKinds{u"qtcraft:rottusk"_s, u"qtcraft:magma_slime"_s, u"qtcraft:wailer"_s, u"qtcraft:lava_wader"_s,
                                        u"qtcraft:skeleton"_s, u"qtcraft:shadewalker"_s};
        QVERIFY(!seen.isEmpty());
        for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
            QVERIFY2(nativeKinds.contains(it.key()), qPrintable(it.key()));
        }
        QVERIFY(seen.contains(u"qtcraft:rottusk"_s));
        QVERIFY(seen.contains(u"qtcraft:lava_wader"_s));
    }

    void nothingStandsOnTheRoofOrOnBedrock()
    {
        const std::unique_ptr<QcServerWorld> world = makeWorld();
        const QHash<QString, QList<QcVec3>> seen = census(*world, QcDayCycle::dayLength * 0.75);
        const QcBlockId bedrock = *m_content->blockId(u"qtcraft:bedrock"_s);
        for (const QList<QcVec3> &places : seen) {
            for (const QcVec3 &at : places) {
                QVERIFY2(at.y < nether().ceiling, qPrintable(QString::number(at.y)));
                const QcBlockPos floor = QcPhysics::blockContaining(at).offset(0, -1, 0);
                QVERIFY(world->blockAt(floor) != bedrock);
            }
        }
    }

    void wadersStartOnLava()
    {
        const std::unique_ptr<QcServerWorld> world = makeWorld();
        const QHash<QString, QList<QcVec3>> seen = census(*world, 0.0);
        const QList<QcVec3> waders = seen.value(u"qtcraft:lava_wader"_s);
        QVERIFY(!waders.isEmpty());
        for (const QcVec3 &at : waders) {
            const std::optional<QcBlockId> floor = world->blockAt(QcPhysics::blockContaining(at).offset(0, -1, 0));
            QVERIFY(floor && m_content->block(*floor).isLava());
        }
    }

    // What a crowd of the nether's kinds costs in the tick, against as many zombies: a measurement
    // (logged, nothing asserted), sprinted, without real time.
    void tickCostOfACrowd()
    {
        const std::unique_ptr<QcServerWorld> world = makeWorld();
        const auto cost = [&](const QStringList &kinds, int each) {
            QcMobManager mobs(m_content, 9);
            mobs.setDimension(nether());
            mobs.setNaturalSpawning(false);
            for (const QString &name : kinds) {
                for (int i = 0; i < each; ++i) {
                    const int x = (i * 13) % 96 - 48;
                    const int z = (i * 29) % 96 - 48;
                    mobs.spawn(*m_content->mobKind(name), {x + 0.5, 70.0, z + 0.5}, m_nextId++);
                }
            }
            const QList<QcMobManager::PlayerView> players{{.entityId = 900000, .position = {8.5, 64.0, 8.5}}};
            QElapsedTimer timer;
            qint64 spent = 0;
            const int ticks = 400;
            for (int tick = 0; tick < ticks; ++tick) {
                timer.start();
                mobs.tick(*world, players, 0.0, QcPhysics::tickDuration, [this] { return m_nextId++; });
                spent += tick >= 100 ? timer.nsecsElapsed() : 0;
            }
            return static_cast<double>(spent) / (ticks - 100) / 1e6;
        };
        qInfo().noquote() << u"ms per tick: 40 zombies %1, 40 rottusks %2, 40 magma slimes %3, 40 wailers %4, 40 waders %5"_s
                                 .arg(cost({u"qtcraft:zombie"_s}, 40), 0, 'f', 3).arg(cost({u"qtcraft:rottusk"_s}, 40), 0, 'f', 3)
                                 .arg(cost({u"qtcraft:magma_slime"_s}, 40), 0, 'f', 3).arg(cost({u"qtcraft:wailer"_s}, 40), 0, 'f', 3)
                                 .arg(cost({u"qtcraft:lava_wader"_s}, 40), 0, 'f', 3);
    }

    // Rottusks come as in the role model (weight 100, groups of 4 in the wastes, the monster cap
    // of 70 per 289 chunks; the human, 2026-11-06) and no more: the most within 64 blocks of the
    // player at any tick of sprinted spawning, with the creatures' own despawning, the monster
    // cap's whole area loaded (as a player's view distance does).
    void rottusksDoNotCrowdTheWastes()
    {
        const std::unique_ptr<QcServerWorld> world = makeWorld(42, QcMonsterCap::reach);
        QcMobManager mobs(m_content, 9);
        mobs.setDimension(nether());
        const QcVec3 player{8.5, 64.0, 8.5};
        const QList<QcMobManager::PlayerView> players{{.entityId = 900000, .position = player}};
        const QcCreatureKindId rottusk = *m_content->mobKind(u"qtcraft:rottusk"_s);
        const QcCreatureKindId wailer = *m_content->mobKind(u"qtcraft:wailer"_s);
        qsizetype most = 0;
        qsizetype mostWailers = 0;
        for (int tick = 0; tick < seconds * QcPhysics::tickRate; ++tick) {
            mobs.tick(*world, players, 0.0, QcPhysics::tickDuration, [this] { return m_nextId++; });
            const auto near = [&](const QcCreature &mob, QcCreatureKindId type) { return mob.type == type && (mob.motion.position - player).length() <= 64.0; };
            most = std::max(most, std::ranges::count_if(mobs.mobs(), [&](const QcCreature &mob) { return near(mob, rottusk); }));
            mostWailers = std::max(mostWailers, std::ranges::count_if(mobs.mobs(), [&](const QcCreature &mob) { return near(mob, wailer); }));
        }
        qInfo() << "most within 64 blocks: rottusks" << most << "wailers" << mostWailers;
        QVERIFY(most > 0);
        QVERIFY2(most <= maxRottusksNear, qPrintable(QString::number(most)));
    }

    void theTimeOfDayDoesNotReachTheNether()
    {
        // The same seed and places: the sky's time changes nothing.
        const std::unique_ptr<QcServerWorld> noon = makeWorld();
        const std::unique_ptr<QcServerWorld> night = makeWorld();
        const auto counts = [](const QHash<QString, QList<QcVec3>> &seen) {
            QHash<QString, qsizetype> sizes;
            for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
                sizes.insert(it.key(), it.value().size());
            }
            return sizes;
        };
        QCOMPARE(counts(census(*noon, QcDayCycle::dayLength * 0.25)), counts(census(*night, QcDayCycle::dayLength * 0.75)));
    }
};

QTEST_GUILESS_MAIN(TestNetherSpawns)
#include "test_nether_spawns.moc"
