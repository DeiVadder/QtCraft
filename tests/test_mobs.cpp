#include "QcBaseContent.h"
#include "QcDayCycle.h"
#include "QcMobManager.h"
#include "QcPhysics.h"
#include "QcServerWorld.h"
#include "QcTestTerrainGenerator.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

constexpr double dt = 0.05;
constexpr double noon = QcDayCycle::dayLength * 0.25;
constexpr double midnight = QcDayCycle::dayLength * 0.75;

} // namespace

class TestMobs : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();
    quint32 m_nextId = 100;

    std::unique_ptr<QcServerWorld> makeWorld(int radius = 4)
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    // Feet position on the generated surface of a column.
    QcVec3 surface(QcServerWorld &world, int x, int z)
    {
        const QcBlockPos pos{x, 0, z};
        return {x + 0.5, double(world.chunk(pos.chunk())->highestNonAir(pos.localX(), pos.localZ()) + 1), z + 0.5};
    }

    int type(const char *name) const
    {
        const std::optional<int> index = m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name));
        if (!index) {
            qFatal("unknown mob %s", name);
        }
        return *index;
    }

private slots:
    void dayCycleFollowsTheSun()
    {
        QCOMPARE(QcDayCycle::phase(0.0), 0.0);
        QVERIFY(std::abs(QcDayCycle::sunHeight(noon) - 1.0) < 1e-12);
        QCOMPARE(QcDayCycle::daylight(noon), 1.0);
        QVERIFY(!QcDayCycle::isNight(noon));
        QVERIFY(QcDayCycle::isNight(midnight));
        QCOMPARE(QcDayCycle::daylight(midnight), QcDayCycle::minimumDaylight);
        // A day later everything repeats.
        QCOMPARE(QcDayCycle::isNight(midnight + QcDayCycle::dayLength), true);
        QVERIFY(std::abs(QcDayCycle::phase(noon + 3 * QcDayCycle::dayLength) - 0.25) < 1e-12);
    }

    void hostileMobChasesAndHitsWithCooldown()
    {
        auto world = makeWorld();
        QcMobManager mobs(m_content, 1);
        const QcVec3 player = surface(*world, 0, 0);
        mobs.spawn(type("zombie"), surface(*world, 6, 0), 1);
        int hits = 0;
        double firstHit = -1.0;
        for (int tick = 0; tick < 20 * 6; ++tick) {
            const QList<QcMobManager::PlayerHit> result =
                mobs.tick(*world, {{7, player, true}}, midnight, dt, [this] { return m_nextId++; });
            for (const QcMobManager::PlayerHit &hit : result) {
                QCOMPARE(hit.playerEntityId, 7u);
                QCOMPARE(hit.damage, 3.0);
                if (firstHit < 0.0) {
                    firstHit = tick * dt;
                }
                ++hits;
            }
        }
        // It closes six metres at chase speed and then strikes once per second.
        QVERIFY2(firstHit > 0.5 && firstHit < 3.5, qPrintable(QString::number(firstHit)));
        const double hittingTime = 6.0 - firstHit;
        QVERIFY2(std::abs(hits - std::floor(hittingTime / 1.0) - 1) <= 1, qPrintable(QString::number(hits)));
    }

    void passiveMobsNeverAttack()
    {
        auto world = makeWorld();
        QcMobManager mobs(m_content, 2);
        const QcVec3 player = surface(*world, 0, 0);
        mobs.spawn(type("pig"), surface(*world, 1, 0), 1);
        for (int tick = 0; tick < 100; ++tick) {
            QVERIFY(mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; }).isEmpty());
        }
    }

    void damageKillsAndReturnsDrops()
    {
        auto world = makeWorld();
        QcMobManager mobs(m_content, 3);
        mobs.spawn(type("pig"), surface(*world, 2, 2), 1);
        // Pig: 10 health. Hits inside the invulnerability window do nothing.
        QVERIFY(!mobs.damage(1, 6.0, {}).has_value());
        QVERIFY(!mobs.damage(1, 6.0, {}).has_value());
        QCOMPARE(mobs.find(1)->health, 4.0);
        // A player nearby keeps the pig from despawning while the invulnerability wears off.
        for (int tick = 0; tick < 11; ++tick) {
            mobs.tick(*world, {{7, surface(*world, 0, 0), true}}, noon, dt, [this] { return m_nextId++; });
        }
        const auto drops = mobs.damage(1, 6.0, {});
        QVERIFY(drops.has_value());
        QCOMPARE(drops->size(), 1);
        QCOMPARE(drops->first().item, u"qtcraft:raw_porkchop"_s);
        QCOMPARE(drops->first().count, 2);
        QVERIFY(!mobs.find(1));
    }

    void mobsFarFromPlayersDespawn()
    {
        auto world = makeWorld(8);
        QcMobManager mobs(m_content, 4);
        mobs.spawn(type("pig"), surface(*world, 100, 0), 1);
        mobs.spawn(type("pig"), surface(*world, 10, 0), 2);
        mobs.tick(*world, {{7, surface(*world, 0, 0), true}}, noon, dt, [this] { return m_nextId++; });
        QVERIFY(!mobs.find(1));
        QVERIFY(mobs.find(2));
    }

    void spawningDependsOnTimeOfDay()
    {
        auto world = makeWorld(5);
        const QcVec3 player = surface(*world, 0, 0);
        const auto runFor = [&](QcMobManager &mobs, double time) {
            for (int tick = 0; tick < 20 * 30; ++tick) {
                mobs.tick(*world, {{7, player, true}}, time, dt, [this] { return m_nextId++; });
            }
        };
        QcMobManager day(m_content, 5);
        runFor(day, noon);
        QVERIFY(!day.mobs().isEmpty());
        for (const QcMobManager::Mob &mob : day.mobs()) {
            const QcMobDefinition &definition = m_content->mobs()[mob.type];
            if (definition.spawnTime != QcMobDefinition::SpawnTime::Night) {
                continue;
            }
            // By day night creatures only live in caves, well below the surface of their column.
            const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
            const std::shared_ptr<const QcChunk> chunk = world->chunk(feet.chunk());
            QVERIFY(chunk);
            QVERIFY2(mob.underground && chunk->highestNonAir(feet.localX(), feet.localZ()) > feet.y + QcMobManager::caveDepth,
                     qPrintable(definition.name));
        }
        QcMobManager night(m_content, 5);
        runFor(night, midnight);
        bool zombie = false;
        for (const QcMobManager::Mob &mob : night.mobs()) {
            zombie = zombie || mob.type == type("zombie");
            const double distance = (mob.motion.position - player).length();
            QVERIFY2(distance < QcMobManager::despawnDistance, qPrintable(QString::number(distance)));
        }
        QVERIFY(zombie);
        int hostile = 0;
        for (const QcMobManager::Mob &mob : night.mobs()) {
            hostile += m_content->mobs()[mob.type].hostile;
        }
        QVERIFY(hostile <= QcMobManager::hostileCapPerPlayer + 3);
    }

    void torchLightKeepsHostileMobsAway()
    {
        const auto surfaceHostiles = [this](bool torches) {
            auto world = makeWorld(5);
            const QcVec3 player = surface(*world, 0, 0);
            if (torches) {
                // A torch every five blocks: no surface spot is more than four blocks from one, so
                // the block light at every surface spawn is at least 14 - 4 - 1 = 9 (> 7).
                const QcBlockId torch = *m_content->blockId(u"qtcraft:torch"_s);
                for (int x = -60; x <= 60; x += 5) {
                    for (int z = -60; z <= 60; z += 5) {
                        const QcVec3 top = surface(*world, x, z);
                        world->setBlock(QcPhysics::blockContaining(top), torch);
                    }
                }
            }
            const int light = world->blockLightAt(QcPhysics::blockContaining(surface(*world, 2, 2)));
            if (torches != (light >= 9)) {
                return -1;
            }
            QcMobManager mobs(m_content, 11);
            int count = 0;
            QSet<quint32> seen;
            for (int tick = 0; tick < 20 * 40; ++tick) {
                mobs.tick(*world, {{7, player, true}}, midnight, dt, [this] { return m_nextId++; });
                for (const QcMobManager::Mob &mob : mobs.mobs()) {
                    if (!seen.contains(mob.id)) {
                        seen.insert(mob.id);
                        count += m_content->mobs()[mob.type].hostile && !mob.underground;
                    }
                }
            }
            return count;
        };
        // Dark nights bring zombies to the surface; a lit area gets none (underground spawns go on).
        QVERIFY(surfaceHostiles(false) > 0);
        QCOMPARE(surfaceHostiles(true), 0);
    }

    void daylightOnlyDespawnsSurfaceNightCreatures()
    {
        auto world = makeWorld(5);
        const QcVec3 player = surface(*world, 0, 0);
        QcMobManager mobs(m_content, 5);
        const quint32 surfaceZombie = mobs.spawn(type("zombie"), player + QcVec3{30.0, 0.0, 0.0}, m_nextId++);
        const quint32 caveZombie = mobs.spawn(type("zombie"), player + QcVec3{-30.0, 0.0, 0.0}, m_nextId++, true);
        mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });
        QVERIFY(!mobs.find(surfaceZombie));
        QVERIFY(mobs.find(caveZombie));
    }
};

QTEST_GUILESS_MAIN(TestMobs)
#include "test_mobs.moc"
