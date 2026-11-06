// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcDayCycle.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/spawning/QcMonsterCap.h"
#include "server/spawning/QcNaturalSpawner.h"
#include "server/spawning/QcSpawnAreas.h"
#include "worldgen/nether/QcNetherGenerator.h"

#include <QElapsedTimer>
#include <QSet>
#include <QTest>

using namespace Qt::StringLiterals;

// Who comes by itself under the roof of the nether (decisions E-1050, E-1051, E-1038): only kinds
// that name its biomes, on floors below the roof, never on bedrock, the same at every time of day;
// lava waders on the lava; in a fortress's parts only its kinds (decision E-1074). Ticks are stepped by hand on a real nether, no server.
class TestNetherSpawns : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    quint32 m_nextId = 1;
    static constexpr int radius = 5;
    static constexpr int seconds = 90;
    // The monster cap spread evenly over where monsters may be: they come only 24 to 128 blocks from
    // the player and vanish beyond 128, so the ring between 24 and 64 blocks holds its share of
    // the ring up to 128, about 15.6 of 70 (decisions E-1050, E-1051).
    static constexpr double nearRadius = 64.0;
    static constexpr double ringNear = nearRadius * nearRadius - QcNaturalSpawner::monsterRadiusMin * QcNaturalSpawner::monsterRadiusMin;
    static constexpr double ringAll = QcNaturalSpawner::monsterRadiusMax * QcNaturalSpawner::monsterRadiusMax - QcNaturalSpawner::monsterRadiusMin * QcNaturalSpawner::monsterRadiusMin;
    static constexpr double capNear = QcNaturalSpawner::monsterCapPer289Chunks * ringNear / ringAll;
    // The time average may lie up to 40 % above the even share (groups of 4 come together); the
    // highest count at any one moment up to three groups of 4 above it.
    static constexpr double maxMeanNear = capNear * 1.4;
    static constexpr double maxMostNear = capNear + 3 * 4;
    // Samples are taken every two seconds after the first twenty, when the cap has filled.
    static constexpr int rampUpSeconds = 20;
    static constexpr int sampleSeconds = 2;

    // How many rottusks and wailers stood within 64 blocks of the player: the time average and the
    // highest count at any tick.
    struct Crowd
    {
        double meanRottusks = 0.0;
        qsizetype mostRottusks = 0;
        qsizetype mostWailers = 0;
    };

    const QcDimensionDefinition &nether() const { return *m_content->worldgen().dimension(u"qtcraft:nether"_s); }

    std::unique_ptr<QcServerWorld> makeWorld(qint64 seed = 4711, int chunks = radius, QcChunkPos centre = {0, 0}) const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcNetherGenerator>(m_content, seed));
        for (int x = -chunks; x <= chunks; ++x) {
            for (int z = -chunks; z <= chunks; ++z) {
                world->loadChunk({centre.x + x, centre.z + z});
            }
        }
        return world;
    }

    // Sprinted spawning for `seconds` around the middle of the chunk `centre`.
    Crowd crowdNear(qint64 seed, QcChunkPos centre)
    {
        const std::unique_ptr<QcServerWorld> world = makeWorld(seed, QcMonsterCap::reach, centre);
        QcMobManager mobs(m_content, 9);
        mobs.setDimension(nether());
        const QcVec3 player{centre.x * QcWorld::chunkSize + 8.5, 64.0, centre.z * QcWorld::chunkSize + 8.5};
        const QList<QcMobManager::PlayerView> players{{.entityId = 900000, .position = player}};
        const QcCreatureKindId rottusk = *m_content->mobKind(u"qtcraft:rottusk"_s);
        const QcCreatureKindId wailer = *m_content->mobKind(u"qtcraft:wailer"_s);
        const auto countNear = [&](QcCreatureKindId type) {
            return std::ranges::count_if(mobs.mobs(), [&](const QcCreature &mob) { return mob.type == type && (mob.motion.position - player).length() <= nearRadius; });
        };

        const int ticksPerSecond = static_cast<int>(QcPhysics::tickRate);
        Crowd crowd;
        qsizetype total = 0;
        int samples = 0;
        for (int tick = 0; tick < seconds * ticksPerSecond; ++tick) {
            mobs.tick(*world, players, 0.0, QcPhysics::tickDuration, [this] { return m_nextId++; });
            const qsizetype rottusks = countNear(rottusk);
            crowd.mostRottusks = std::max(crowd.mostRottusks, rottusks);
            crowd.mostWailers = std::max(crowd.mostWailers, countNear(wailer));
            if (tick >= rampUpSeconds * ticksPerSecond && tick % (sampleSeconds * ticksPerSecond) == 0) {
                total += rottusks;
                ++samples;
            }
        }
        crowd.meanRottusks = static_cast<double>(total) / samples;
        return crowd;
    }

    // The kinds that came in `seconds` of sprinted ticks at the world time `time`, with where they
    // stood when they came.
    // With `removeSeen` every creature is removed in the tick it came, so the cap never fills.
    QHash<QString, QList<QcVec3>> census(QcServerWorld &world, double time, const QcVec3 &player = {8.5, 64.0, 8.5}, bool removeSeen = false)
    {
        QcMobManager mobs(m_content, 9);
        mobs.setDimension(nether());
        const QList<QcMobManager::PlayerView> players{{.entityId = 900000, .position = player}};
        // The ids from the start: two censuses with the same seed must see the same creatures.
        m_nextId = 1;
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
            if (removeSeen) {
                mobs.removeAll();
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
                                        u"qtcraft:skeleton"_s, u"qtcraft:shadewalker"_s, u"qtcraft:tuskling"_s, u"qtcraft:bristleboar"_s};
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
        qInfo().noquote() << u"ms per tick: 40 zombies %1, 40 rottusks %2, 40 magma slimes %3, 40 wailers %4, 40 waders %5, 40 ember wraiths %6, 40 charred skeletons %7"_s
                                 .arg(cost({u"qtcraft:zombie"_s}, 40), 0, 'f', 3).arg(cost({u"qtcraft:rottusk"_s}, 40), 0, 'f', 3)
                                 .arg(cost({u"qtcraft:magma_slime"_s}, 40), 0, 'f', 3).arg(cost({u"qtcraft:wailer"_s}, 40), 0, 'f', 3)
                                 .arg(cost({u"qtcraft:lava_wader"_s}, 40), 0, 'f', 3).arg(cost({u"qtcraft:ember_wraith"_s}, 40), 0, 'f', 3)
                                 .arg(cost({u"qtcraft:charred_skeleton"_s}, 40), 0, 'f', 3);
        // The gold folk (decision E-1086): with a player who wears no gold, so tusklings hunt him.
        qInfo().noquote() << u"ms per tick: 40 zombies %1, 40 tusklings %2, 40 bristleboars %3"_s.arg(cost({u"qtcraft:zombie"_s}, 40), 0, 'f', 3)
                                 .arg(cost({u"qtcraft:tuskling"_s}, 40), 0, 'f', 3).arg(cost({u"qtcraft:bristleboar"_s}, 40), 0, 'f', 3);
    }

    // The kinds of a fortress come in groups, and a group never goes past the room under the cap.
    void fortressGroupsComeTogetherAndKeepTheCap()
    {
        const qint64 seed = 42;
        const QcNetherGenerator probe(m_content, seed);
        std::shared_ptr<const QcFortressPlan> plan;
        for (int i = 0; i < 16 && !plan; ++i) {
            plan = probe.fortress().planOf(i % 4 - 2, i / 4 - 2);
        }
        QVERIFY(plan);
        const QcBlockPos middle = plan->middle();
        const std::unique_ptr<QcServerWorld> world = makeWorld(seed, QcMonsterCap::reach, middle.chunk());
        const QList<QcMobManager::PlayerView> players{{.entityId = 900000, .position = {middle.x + 0.5, middle.y + 0.0, middle.z + 0.5}}};
        const auto inFortress = [&plan](const QcCreature &mob) {
            const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
            return std::ranges::any_of(plan->parts, [feet](const QcBlockBox &part) { return part.contains(feet); });
        };
        // What came in one tick: all monsters, and the most of one kind side by side in the fortress.
        // Every tick starts empty, so the room under the cap is the whole cap each time.
        struct Most
        {
            qsizetype monsters = 0;
            qsizetype together = 0;
        };
        const auto run = [&](int cap) {
            QcMobManager mobs(m_content, 9);
            mobs.setDimension(nether());
            mobs.spawner().setCaps(cap, 0, 0);
            Most most;
            for (int tick = 0; tick < 60 * QcPhysics::tickRate; ++tick) {
                mobs.tick(*world, players, 0.0, QcPhysics::tickDuration, [this] { return m_nextId++; });
                const auto monster = [this](const QcCreature &mob) { return m_content->mobs()[mob.type].isMonster(); };
                most.monsters = std::max<qsizetype>(most.monsters, std::ranges::count_if(mobs.mobs(), monster));
                for (const QcCreature &mob : mobs.mobs()) {
                    const auto beside = [&](const QcCreature &other) {
                        return other.type == mob.type && inFortress(other) && (other.motion.position - mob.motion.position).length() < 4.0;
                    };
                    most.together = std::max<qsizetype>(most.together, inFortress(mob) ? std::ranges::count_if(mobs.mobs(), beside) : 0);
                }
                mobs.removeAll();
            }
            return most;
        };
        const qsizetype together = run(QcNaturalSpawner::monsterCapPer289Chunks).together;
        qInfo() << "most side by side" << together;
        QVERIFY2(together >= 2, qPrintable(QString::number(together)));
        QCOMPARE(run(1).monsters, qsizetype{1});
    }

    void rottusksDoNotCrowdTheWastes_data()
    {
        QTest::addColumn<QcChunkPos>("centre");
        QTest::newRow("near a fortress") << QcChunkPos{0, 0};
        QTest::newRow("far from any") << QcChunkPos{80, 0};
    }

    // Rottusks come as in the role model (weight 100, groups of 4 in the wastes, the monster cap
    // of 70 per 289 chunks; the human, 2026-11-06) and no more: on average over sprinted spawning
    // no more than the cap's share near the player allows, and never a crowd at any tick, with the
    // creatures' own despawning, the monster cap's whole area loaded (as a player's view distance
    // does). Measured at seed 42 beside a fortress and far from any spawn area.
    void rottusksDoNotCrowdTheWastes()
    {
        QFETCH(QcChunkPos, centre);
        const Crowd crowd = crowdNear(42, centre);
        qInfo() << "within 64 blocks: rottusks on average" << crowd.meanRottusks << "at most" << crowd.mostRottusks << "wailers at most" << crowd.mostWailers << "share of the cap" << capNear;
        QVERIFY(crowd.mostRottusks > 0);
        QVERIFY2(crowd.meanRottusks <= maxMeanNear, qPrintable(QString::number(crowd.meanRottusks)));
        QVERIFY2(static_cast<double>(crowd.mostRottusks) <= maxMostNear, qPrintable(QString::number(crowd.mostRottusks)));
    }

    // In the boxes of a fortress's parts only its kinds of monsters come (decision E-1074); the
    // skeletons there stand in block light of at most 7.
    void aFortressBringsForthItsKindsInItsParts()
    {
        const qint64 seed = 42;
        const QcNetherGenerator probe(m_content, seed);
        std::shared_ptr<const QcFortressPlan> plan;
        for (int i = 0; i < 16 && !plan; ++i) {
            plan = probe.fortress().planOf(i % 4 - 2, i / 4 - 2);
        }
        QVERIFY(plan);
        const QcBlockPos middle = plan->middle();
        const std::unique_ptr<QcServerWorld> world = makeWorld(seed, radius, middle.chunk());
        QCOMPARE(world->generator().structureSitesNear(middle, QcSpawnAreas::reach).spawnAreas.size(), qsizetype{1});
        const QHash<QString, QList<QcVec3>> seen = census(*world, 0.0, {middle.x + 0.5, middle.y + 0.0, middle.z + 0.5}, true);
        const QSet<QString> fortressKinds{u"qtcraft:ember_wraith"_s, u"qtcraft:rottusk"_s, u"qtcraft:charred_skeleton"_s, u"qtcraft:skeleton"_s,
                                          u"qtcraft:magma_slime"_s};
        int inside = 0;
        int outside = 0;
        for (auto it = seen.cbegin(); it != seen.cend(); ++it) {
            for (const QcVec3 &at : it.value()) {
                const QcBlockPos feet = QcPhysics::blockContaining(at);
                const bool inPart = std::ranges::any_of(plan->parts, [feet](const QcBlockBox &part) { return part.contains(feet); });
                // Creatures that are no monsters (lava waders on the sea under a bridge) keep their
                // own rules, as the striders of the original game do.
                const bool monster = m_content->mobs()[*m_content->mobKind(it.key())].isMonster();
                QVERIFY2(!inPart || !monster || fortressKinds.contains(it.key()), qPrintable(u"%1 at %2 %3 %4"_s.arg(it.key()).arg(feet.x).arg(feet.y).arg(feet.z)));
                inside += inPart && monster ? 1 : 0;
                outside += inPart ? 0 : 1;
                const bool dark = it.key() == u"qtcraft:skeleton"_s || it.key() == u"qtcraft:charred_skeleton"_s;
                QVERIFY2(!inPart || !dark || world->blockLight(feet) <= 7, qPrintable(u"%1 at %2 %3 %4"_s.arg(it.key()).arg(feet.x).arg(feet.y).arg(feet.z)));
            }
        }
        qInfo() << "fortress kinds in its parts:" << inside << "others outside:" << outside << seen.keys();
        QVERIFY(inside > 0);
        QVERIFY(seen.contains(u"qtcraft:ember_wraith"_s));
        QVERIFY(seen.contains(u"qtcraft:charred_skeleton"_s));
    }

    // Natural spawns come young by `young.chance` (decision E-1083): both kinds of the crimson forest
    // and the wastes, a fifth of them. Spawned creatures are removed at once, so the cap never fills.
    void aFifthOfTheGoldFolkComesYoung()
    {
        const std::unique_ptr<QcServerWorld> world = makeWorld();
        QcMobManager mobs(m_content, 9);
        mobs.setDimension(nether());
        const QList<QcMobManager::PlayerView> players{{.entityId = 900000, .position = {8.5, 64.0, 8.5}}};
        const QcCreatureKindId kinds[] = {*m_content->mobKind(u"qtcraft:tuskling"_s), *m_content->mobKind(u"qtcraft:bristleboar"_s)};
        m_nextId = 1;
        int seen = 0;
        int young = 0;
        for (int tick = 0; tick < 20 * seconds * QcPhysics::tickRate && seen < 400; ++tick) {
            mobs.tick(*world, players, 0.0, QcPhysics::tickDuration, [this] { return m_nextId++; });
            for (const QcCreature &mob : mobs.mobs()) {
                if (std::ranges::find(kinds, mob.type) != std::end(kinds)) {
                    ++seen;
                    young += mob.isYoung() ? 1 : 0;
                }
            }
            mobs.removeAll();
        }
        qInfo() << "young" << young << "of" << seen;
        QVERIFY2(seen >= 200, qPrintable(QString::number(seen)));
        const double share = static_cast<double>(young) / seen;
        QVERIFY2(std::abs(share - 0.2) < 0.08, qPrintable(QString::number(share)));
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
