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

    // Puts `block` on (dy 0) or above (dy 1) the top block of every column in reach of the spawning
    // around the origin.
    void coverSurface(QcServerWorld &world, const QcContentRegistry &content, const char *block, int dy) const
    {
        const QcBlockId id = *content.blockId(u"qtcraft:"_s + QString::fromLatin1(block));
        const int reach = int(QcMobManager::spawnRadiusMax) + 2;
        for (int x = -reach; x <= reach; ++x) {
            for (int z = -reach; z <= reach; ++z) {
                const QcBlockPos column{x, 0, z};
                const int top = world.chunk(column.chunk())->highestNonAir(column.localX(), column.localZ());
                world.setBlock({x, top + dy, z}, id);
            }
        }
    }

    // Zombies that appear within 30 s of a night, with the surface covered by `block` if given.
    int spawnedZombies(const std::shared_ptr<const QcContentRegistry> &content, const char *block, int dy = 0)
    {
        auto world = std::make_unique<QcServerWorld>(content, std::make_unique<QcTestTerrainGenerator>(content));
        for (int x = -4; x <= 4; ++x) {
            for (int z = -4; z <= 4; ++z) {
                world->loadChunk({x, z});
            }
        }
        const QcVec3 player = surface(*world, 0, 0);
        if (block) {
            coverSurface(*world, *content, block, dy);
        }

        QcMobManager mobs(content, 41);
        const int zombie = *content->mobIndex(u"qtcraft:zombie"_s);
        for (int tick = 0; tick < 20 * 30; ++tick) {
            mobs.tick(*world, {{7, player, true}}, midnight, dt, [this] { return m_nextId++; });
        }
        return int(std::ranges::count(mobs.mobs(), zombie, &QcMobManager::Mob::type));
    }

    // Animals the chunks around the origin get, with `block` over the surface if given.
    int populatedAnimals(const char *block)
    {
        auto world = makeWorld(4);
        if (block) {
            coverSurface(*world, *m_content, block, 1);
        }

        QcMobManager mobs(m_content, 21);
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                mobs.populateChunk(*world, {x, z}, [this] { return m_nextId++; });
            }
        }
        return int(mobs.mobs().size());
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

    void newChunksGetPersistentAnimals()
    {
        auto world = makeWorld(12);
        QcMobManager mobs(m_content, 21);
        int groups = 0;
        int chunks = 0;

        for (int x = -12; x <= 12; ++x) {
            for (int z = -12; z <= 12; ++z) {
                const qsizetype before = mobs.mobs().size();
                mobs.populateChunk(*world, {x, z}, [this] { return m_nextId++; });
                ++chunks;
                QVERIFY(mobs.isPopulated({x, z}));
                groups += mobs.mobs().size() > before;

                // A second call never adds more.
                const qsizetype after = mobs.mobs().size();
                mobs.populateChunk(*world, {x, z}, [this] { return m_nextId++; });
                QCOMPARE(mobs.mobs().size(), after);
            }
        }

        // At most about chunkAnimalChance of the 625 chunks get a group (fewer where the chosen
        // column is sand or water).
        const double share = double(groups) / chunks;
        QVERIFY2(share > 0.03 && share < QcMobManager::chunkAnimalChance + 0.04, qPrintable(QString::number(share)));

        for (const QcMobManager::Mob &mob : mobs.mobs()) {
            const QcMobDefinition &definition = m_content->mobs()[mob.type];
            QVERIFY(!definition.hostile && !definition.ambient && mob.persistent);
            const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
            QCOMPARE(m_content->block(*world->blockAt(feet.offset(0, -1, 0))).name, u"qtcraft:grass_block"_s);
            QVERIFY(!m_content->block(*world->blockAt(feet)).solid);
        }

        // The same seed gives the same animals; a chunk that is not loaded is left for later.
        QcMobManager again(m_content, 21);
        for (int x = -12; x <= 12; ++x) {
            for (int z = -12; z <= 12; ++z) {
                again.populateChunk(*world, {x, z}, [this] { return m_nextId++; });
            }
        }

        QCOMPARE(again.mobs().size(), mobs.mobs().size());
        for (qsizetype i = 0; i < mobs.mobs().size(); ++i) {
            QCOMPARE(again.mobs()[i].motion.position, mobs.mobs()[i].motion.position);
            QCOMPARE(again.mobs()[i].type, mobs.mobs()[i].type);
        }
        again.populateChunk(*world, {40, 40}, [this] { return m_nextId++; });
        QVERIFY(!again.isPopulated({40, 40}));
    }

    void animalsRestInUnloadedChunksInsteadOfDespawning()
    {
        auto world = makeWorld(8);
        QcMobManager mobs(m_content, 22);
        const QcVec3 farAway = surface(*world, 120, 0);
        const QcVec3 player = surface(*world, 0, 0);
        mobs.spawn(type("pig"), farAway, 1, false, true);
        mobs.spawn(type("pig"), surface(*world, 110, 0), 2);
        mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });

        // The persistent pig stays far from every player; the ordinary one despawns.
        QVERIFY(mobs.find(1));
        QVERIFY(!mobs.find(2));
        QVERIFY(!mobs.find(1)->dormant);

        // Its chunk goes away: it rests where it is, is not counted and does not move.
        world->unloadChunk(QcPhysics::blockContaining(mobs.find(1)->motion.position).chunk());
        mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });
        const QcVec3 resting = mobs.find(1)->motion.position;
        for (int tick = 0; tick < 100; ++tick) {
            mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });
        }
        QVERIFY(mobs.find(1)->dormant);
        QCOMPARE(mobs.find(1)->motion.position, resting);
        QCOMPARE(mobs.awakeCount(), mobs.mobs().size() - 1);

        // Saved and restored, it comes back dormant with the remembered chunks.
        QcMobManager loaded(m_content, 22);
        loaded.restore(mobs.persistentMobs(), {{1, 2}}, [this] { return m_nextId++; });

        // Animals that spawned near the player meanwhile are persistent too.
        QCOMPARE(loaded.mobs().size(), mobs.persistentMobs().size());
        const auto restored = std::ranges::find(loaded.mobs(), resting, [](const QcMobManager::Mob &mob) { return mob.motion.position; });
        QVERIFY(restored != loaded.mobs().end());
        const quint32 restoredId = restored->id;
        for (const QcMobManager::Mob &mob : loaded.mobs()) {
            QVERIFY(mob.dormant && mob.persistent);
        }
        QVERIFY(loaded.isPopulated({1, 2}));
        world->loadChunk(QcPhysics::blockContaining(resting).chunk());
        loaded.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });
        QVERIFY(!loaded.find(restoredId)->dormant);
    }

    void animalsOnlySpawnWhereNoneAreAround()
    {
        auto world = makeWorld(6);
        const QcVec3 player = surface(*world, 0, 0);
        bool allAnimalsPersistent = true;
        const auto animalsSpawned = [&](bool withAnimal) -> qsizetype {
            QcMobManager mobs(m_content, 23);
            if (withAnimal) {
                mobs.spawn(type("sheep"), surface(*world, 60, 60), 1, false, true);
            }

            const qsizetype before = mobs.mobs().size();
            for (int tick = 0; tick < 20 * 30; ++tick) {
                mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });
            }

            qsizetype animals = 0;
            for (const QcMobManager::Mob &mob : mobs.mobs()) {
                const QcMobDefinition &definition = m_content->mobs()[mob.type];
                animals += !definition.hostile && !definition.ambient;
                allAnimalsPersistent = allAnimalsPersistent && (definition.hostile || definition.ambient || mob.persistent);
            }
            return animals - before;
        };

        QVERIFY(animalsSpawned(false) > 0);

        // A sheep 85 m away is within animalSearchDistance: no new animals.
        QCOMPARE(animalsSpawned(true), qsizetype(0));
        QVERIFY(allAnimalsPersistent);
    }

    void extraSpawnRulesAddPlacesAndTimes()
    {
        // In this world (no biomes, no caves) slimes only come through an extra rule.
        const auto surfaceSlimes = [&](const std::shared_ptr<const QcContentRegistry> &content, double time) {
            auto world = std::make_unique<QcServerWorld>(content, std::make_unique<QcTestTerrainGenerator>(content));
            for (int x = -5; x <= 5; ++x) {
                for (int z = -5; z <= 5; ++z) {
                    world->loadChunk({x, z});
                }
            }

            QcMobManager mobs(content, 31);
            const QcVec3 player = surface(*world, 0, 0);
            const int slime = *content->mobIndex(u"qtcraft:slime"_s);
            QSet<quint32> seen;
            for (int tick = 0; tick < 20 * 40; ++tick) {
                mobs.tick(*world, {{7, player, true}}, time, dt, [this] { return m_nextId++; });
                for (const QcMobManager::Mob &mob : mobs.mobs()) {
                    if (mob.type == slime && !mob.underground) {
                        seen.insert(mob.id);
                    }
                }
            }
            return seen.size();
        };

        // The base slime's swamp rule needs a swamp.
        QCOMPARE(surfaceSlimes(m_content, midnight), qsizetype(0));
        auto everywhere = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition slime = everywhere->mobs()[type("slime")];
        QCOMPARE(slime.extraSpawns.size(), 1);
        slime.extraSpawns.first().biomes.clear();
        everywhere->registerMob(slime);
        QVERIFY(surfaceSlimes(everywhere, midnight) > 0);

        // The rule is for the night: none by day.
        QCOMPARE(surfaceSlimes(everywhere, noon), qsizetype(0));

        // A slime of the night rule goes with the daylight once nobody is near, like night creatures.
        auto world = makeWorld(5);
        QcMobManager mobs(everywhere, 32);
        const QcVec3 player = surface(*world, 0, 0);
        bool spawned = false;
        for (int tick = 0; tick < 20 * 40 && !spawned; ++tick) {
            mobs.tick(*world, {{7, player, true}}, midnight, dt, [this] { return m_nextId++; });
            spawned = std::ranges::any_of(mobs.mobs(), [&](const QcMobManager::Mob &mob) {
                return mob.type == type("slime") && mob.nightRule && (mob.motion.position - player).length() > 24.0;
            });
        }
        QVERIFY(spawned);

        mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });
        QVERIFY(std::ranges::none_of(mobs.mobs(), [&](const QcMobManager::Mob &mob) {
            return mob.nightRule && (mob.motion.position - player).length() > 24.0;
        }));
    }

    void slimeChunksAreFixedBySeed()
    {
        const int slime = type("slime");
        QVERIFY(m_content->mobs()[slime].spawnChunkChance < 1.0);
        QVERIFY(m_content->mobs()[slime].spawnMaxY < QcWorld::seaLevel);

        QcMobManager mobs(m_content, 24);
        QcMobManager same(m_content, 24);
        QcMobManager other(m_content, 25);
        int slimeChunks = 0;
        int differences = 0;

        for (int x = -50; x < 50; ++x) {
            for (int z = -50; z < 50; ++z) {
                const bool here = mobs.spawnsInChunk(slime, {x, z});
                QCOMPARE(same.spawnsInChunk(slime, {x, z}), here);
                slimeChunks += here;
                differences += other.spawnsInChunk(slime, {x, z}) != here;
                // Kinds without the rule spawn everywhere.
                QVERIFY(mobs.spawnsInChunk(type("zombie"), {x, z}));
            }
        }

        const double share = slimeChunks / 10000.0;
        QVERIFY2(std::abs(share - m_content->mobs()[slime].spawnChunkChance) < 0.02, qPrintable(QString::number(share)));
        QVERIFY(differences > 1000);
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

    void noSpawnsOnHurtingOrInHoldingBlocks()
    {
        // Zombies that may stand on any solid block: the block fields keep them off magma
        // (contactDamage) and out of cobwebs (slowsInside), whatever the rule lists.
        auto anywhere = std::make_shared<QcContentRegistry>(*m_content);
        QcMobDefinition zombie = anywhere->mobs()[type("zombie")];
        zombie.spawnBlocks.clear();
        anywhere->registerMob(zombie);

        QVERIFY(spawnedZombies(anywhere, nullptr) > 0);
        QCOMPARE(spawnedZombies(anywhere, "magma_block", 0), 0);
        QCOMPARE(spawnedZombies(anywhere, "cobweb", 1), 0);

        // Animals do not appear in cobwebs over their grass either.
        QVERIFY(populatedAnimals(nullptr) > 0);
        QCOMPARE(populatedAnimals("cobweb"), 0);
    }
};

QTEST_GUILESS_MAIN(TestMobs)
#include "test_mobs.moc"
