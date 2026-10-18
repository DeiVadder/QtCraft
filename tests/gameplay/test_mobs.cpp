#include "common/content/loader/QcBaseContent.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "common/world/QcLightVolume.h"
#include "server/core/QcServerWorld.h"
#include "server/entities/QcEntities.h"
#include "server/mobs/QcMobManager.h"
#include "server/mobs/QcMobUpkeep.h"
#include "server/mobs/QcNaturalSpawner.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QElapsedTimer>
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

    // Creatures in chunks that are not loaded rest, as the server's entity register decides at the
    // start of each tick.
    static void settle(QcMobManager &mobs, const QcServerWorld &world)
    {
        QcEntities::settle(mobs.editableMobs(), [&world](QcChunkPos chunk) { return world.chunk(chunk) != nullptr; });
    }

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
        const int reach = int(QcNaturalSpawner::spawnRadiusMax) + 2;
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
        return int(std::ranges::count(mobs.mobs(), zombie, &QcCreature::type));
    }

    // Animals the chunks around the origin get, with `block` over the surface (dy 1, or `dy`) if
    // given.
    int populatedAnimals(const char *block, int dy = 1)
    {
        auto world = makeWorld(4);
        if (block) {
            coverSurface(*world, *m_content, block, dy);
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
        QVERIFY(!mobs.damage({.creature = 1, .damage = 6.0, .from = {}}).has_value());
        QVERIFY(!mobs.damage({.creature = 1, .damage = 6.0, .from = {}}).has_value());
        QCOMPARE(mobs.find(1)->health, 4.0);

        // A player nearby keeps the pig from despawning while the invulnerability wears off.
        for (int tick = 0; tick < 11; ++tick) {
            mobs.tick(*world, {{7, surface(*world, 0, 0), true}}, noon, dt, [this] { return m_nextId++; });
        }
        const auto drops = mobs.damage({.creature = 1, .damage = 6.0, .from = {}});
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
        QVERIFY2(share > 0.03 && share < QcNaturalSpawner::chunkAnimalChance + 0.04, qPrintable(QString::number(share)));

        for (const QcCreature &mob : mobs.mobs()) {
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
        settle(mobs, *world);
        mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });

        // The persistent pig stays far from every player; the ordinary one despawns.
        QVERIFY(mobs.find(1));
        QVERIFY(!mobs.find(2));
        QVERIFY(!mobs.find(1)->dormant);

        // Its chunk goes away: it rests where it is, is not counted and does not move.
        world->unloadChunk(QcPhysics::blockContaining(mobs.find(1)->motion.position).chunk());
        settle(mobs, *world);
        mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });
        const QcVec3 resting = mobs.find(1)->motion.position;
        for (int tick = 0; tick < 100; ++tick) {
            settle(mobs, *world);
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
        const auto restored = std::ranges::find(loaded.mobs(), resting, [](const QcCreature &mob) { return mob.motion.position; });
        QVERIFY(restored != loaded.mobs().end());
        const quint32 restoredId = restored->id;
        for (const QcCreature &mob : loaded.mobs()) {
            QVERIFY(mob.dormant && mob.persistent);
        }
        QVERIFY(loaded.isPopulated({1, 2}));
        world->loadChunk(QcPhysics::blockContaining(resting).chunk());
        settle(loaded, *world);
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
            for (const QcCreature &mob : mobs.mobs()) {
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
                for (const QcCreature &mob : mobs.mobs()) {
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
        // Without the rare enderman (decision E-460), whose share of the night's places would shift
        // the random sequence this test was laid out with.
        QcMobDefinition enderman = everywhere->mobs()[type("enderman")];
        enderman.spawnWeight = 0.0;
        everywhere->registerMob(enderman);
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
            spawned = std::ranges::any_of(mobs.mobs(), [&](const QcCreature &mob) {
                return mob.type == type("slime") && mob.nightRule && (mob.motion.position - player).length() > 24.0;
            });
        }
        QVERIFY(spawned);

        mobs.tick(*world, {{7, player, true}}, noon, dt, [this] { return m_nextId++; });
        QVERIFY(std::ranges::none_of(mobs.mobs(), [&](const QcCreature &mob) {
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
                const bool here = mobs.spawner().spawnsInChunk(slime, {x, z});
                QCOMPARE(same.spawner().spawnsInChunk(slime, {x, z}), here);
                slimeChunks += here;
                differences += other.spawner().spawnsInChunk(slime, {x, z}) != here;
                // Kinds without the rule spawn everywhere.
                QVERIFY(mobs.spawner().spawnsInChunk(type("zombie"), {x, z}));
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

    void noDespawnWhileNoPlayerIsInTheWorld()
    {
        // Without anybody playing (before the join, after the last player left) the distance and
        // the daylight decide nothing: nobody is far away. The void still takes every creature.
        auto world = makeWorld(5);
        QcMobManager mobs(m_content, 4);
        const quint32 zombie = mobs.spawn(type("zombie"), surface(*world, 10, 0), m_nextId++);
        const quint32 slime = mobs.spawn(type("slime"), surface(*world, -10, 0), m_nextId++);
        const quint32 fallen = mobs.spawn(type("zombie"), {0.5, QcWorld::voidLimitY - 10.0, 0.5}, m_nextId++);
        for (int i = 0; i < 20; ++i) {
            mobs.tick(*world, {}, noon, dt, [this] { return m_nextId++; });
        }

        const bool zombieStays = mobs.find(zombie) != nullptr;
        const bool slimeStays = mobs.find(slime) != nullptr;
        const bool fallenGone = mobs.find(fallen) == nullptr;
        QVERIFY(zombieStays);
        QVERIFY(slimeStays);
        QVERIFY(fallenGone);

        // With a player in the world again the rules apply: 200 m away from it both go.
        mobs.tick(*world, {{7, surface(*world, 0, 0) + QcVec3{200.0, 0.0, 0.0}, true}}, noon, dt, [this] { return m_nextId++; });
        const bool zombieGone = mobs.find(zombie) == nullptr;
        const bool slimeGone = mobs.find(slime) == nullptr;
        QVERIFY(zombieGone);
        QVERIFY(slimeGone);
    }

    void creaturesInsideBlocksArePushedOut()
    {
        // A pig and a zombie with their feet one block deep in the ground (summoned into a hill,
        // a block placed on them): out on top within one tick, so they do not suffocate.
        auto world = makeWorld(5);
        QcMobManager mobs(m_content, 4);
        const QcVec3 pigGround = surface(*world, 2, 0);
        const QcVec3 zombieGround = surface(*world, -2, 3);
        const quint32 pig = mobs.spawn(type("pig"), pigGround - QcVec3{0.0, 1.0, 0.0}, m_nextId++, false, true);
        const quint32 zombie = mobs.spawn(type("zombie"), zombieGround - QcVec3{0.0, 1.0, 0.0}, m_nextId++);
        mobs.tick(*world, {{7, surface(*world, 0, 0), true}}, noon, dt, [this] { return m_nextId++; });

        const double pigFeet = mobs.find(pig)->motion.position.y;
        const double zombieFeet = mobs.find(zombie)->motion.position.y;
        // On top of the block they were in, the nearest clear place, not higher (they walk on in
        // the same tick, so only the height is fixed here).
        QCOMPARE(pigFeet, pigGround.y);
        QCOMPARE(zombieFeet, zombieGround.y);

        // The whole place, without the walk: straight up onto the block.
        QcCreature sunk;
        sunk.type = type("pig");
        sunk.motion.position = pigGround - QcVec3{0.0, 0.6, 0.0};
        QcMobUpkeep::leaveBlocks(sunk, m_content->mobs()[sunk.type], *world);
        const QcVec3 placed = sunk.motion.position;
        QCOMPARE(placed.x, pigGround.x);
        QCOMPARE(placed.y, pigGround.y);
        QCOMPARE(placed.z, pigGround.z);

        // Deep in solid rock there is nowhere to go: the creature stays where it is.
        const QcVec3 buried{8.5, 10.0, 8.5};
        const quint32 stuck = mobs.spawn(type("pig"), buried, m_nextId++, false, true);
        mobs.tick(*world, {{7, surface(*world, 0, 0), true}}, noon, dt, [this] { return m_nextId++; });
        const double stuckFeet = mobs.find(stuck)->motion.position.y;
        QCOMPARE(stuckFeet, buried.y);
    }

    void creaturesBesidePartialShapesStayPut()
    {
        // Creatures wandering for 30 s among fence posts, walls, slabs and pistons: they bump into
        // them but are never moved out of a block (no jump of a metre or more in one tick).
        auto world = makeWorld(5);
        const QcVec3 centre = surface(*world, 0, 0);
        const QcBlockPos base = QcPhysics::blockContaining(centre);
        const QList<const char *> shapes{"oak_fence", "cobblestone_wall", "oak_slab", "signal_piston"};
        for (int dx = -4; dx <= 4; ++dx) {
            for (int dz = -4; dz <= 4; ++dz) {
                world->setBlock(base.offset(dx, -1, dz), *m_content->blockId(u"qtcraft:stone"_s));
                for (int dy = 0; dy <= 3; ++dy) {
                    world->setBlock(base.offset(dx, dy, dz), qcAirBlockId);
                }
                // A stone border, inside it a shape on every second cell, the middle free.
                const bool border = std::abs(dx) == 4 || std::abs(dz) == 4;
                const bool shapeCell = (dx + dz) % 2 == 0 && (dx != 0 || dz != 0);
                const char *shape = border ? "stone" : shapes[std::size_t(dx + 4) % shapes.size()];
                if (border || shapeCell) {
                    world->setBlock(base.offset(dx, 0, dz), *m_content->blockId(u"qtcraft:"_s + QString::fromLatin1(shape)));
                }
            }
        }

        QcMobManager mobs(m_content, 9);
        QList<quint32> ids;
        for (const char *kind : {"pig", "zombie", "slime", "sheep"}) {
            ids.append(mobs.spawn(type(kind), centre, m_nextId++, false, true));
        }
        QHash<quint32, QcVec3> last;
        int jumps = 0;
        for (int tick = 0; tick < 600; ++tick) {
            mobs.tick(*world, {{7, centre + QcVec3{0.0, 0.0, 40.0}, true}}, noon, dt, [this] { return m_nextId++; });
            for (const quint32 mobId : std::as_const(ids)) {
                const QcVec3 now = mobs.find(mobId)->motion.position;
                jumps += last.contains(mobId) && (now - last.value(mobId)).length() >= 1.0 ? 1 : 0;
                last.insert(mobId, now);
            }
        }
        QCOMPARE(jumps, 0);
    }

    void measureWalkingCreatures()
    {
        // Cost of the creature tick with 1000 wandering pigs (QTCRAFT_MOB_MEASURE=1, Release build);
        // a measurement, not a check.
        if (qEnvironmentVariableIntValue("QTCRAFT_MOB_MEASURE") != 1) {
            QSKIP("measurement: set QTCRAFT_MOB_MEASURE=1");
        }

        auto world = makeWorld(3);
        const QcVec3 centre = surface(*world, 0, 0);
        QcMobManager mobs(m_content, 3);
        for (int i = 0; i < 1000; ++i) {
            mobs.spawn(type("pig"), surface(*world, i % 40 - 20, i / 40 - 12), m_nextId++, false, true);
        }
        const QList<QcMobManager::PlayerView> players{{7, centre, true}};
        for (int tick = 0; tick < 100; ++tick) {
            mobs.tick(*world, players, noon, dt, [this] { return m_nextId++; });
        }

        QElapsedTimer timer;
        timer.start();
        constexpr int ticks = 400;
        for (int tick = 0; tick < ticks; ++tick) {
            mobs.tick(*world, players, noon, dt, [this] { return m_nextId++; });
        }
        const double msPerTick = double(timer.nsecsElapsed()) / 1e6 / ticks;
        qInfo("MEASURE 1000 walking pigs: %.4f ms per tick (%lld creatures)", msPerTick, qlonglong(mobs.mobs().size()));
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
        for (const QcCreature &mob : day.mobs()) {
            const QcMobDefinition &definition = m_content->mobs()[mob.type];
            if (definition.spawnTime != QcMobDefinition::SpawnTime::Night) {
                continue;
            }
            // By day night creatures only live in caves, well below the surface of their column.
            const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
            const std::shared_ptr<const QcChunk> chunk = world->chunk(feet.chunk());
            QVERIFY(chunk);
            QVERIFY2(mob.underground && chunk->highestNonAir(feet.localX(), feet.localZ()) > feet.y + QcNaturalSpawner::caveDepth,
                     qPrintable(definition.name));
        }

        QcMobManager night(m_content, 5);
        runFor(night, midnight);
        // Night creatures come to the surface; which kinds depends on the random draws among them.
        bool nightCreature = false;
        for (const QcCreature &mob : night.mobs()) {
            nightCreature = nightCreature || (m_content->mobs()[mob.type].spawnTime == QcMobDefinition::SpawnTime::Night && !mob.underground);
            const double distance = (mob.motion.position - player).length();
            QVERIFY2(distance < QcMobManager::despawnDistance, qPrintable(QString::number(distance)));
        }

        QVERIFY(nightCreature);
        int hostile = 0;
        for (const QcCreature &mob : night.mobs()) {
            hostile += m_content->mobs()[mob.type].hostile;
        }
        QVERIFY(hostile <= QcNaturalSpawner::hostileCapPerPlayer + 3);
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

            // The server's light engine gives the same block light as a flood fill on demand.
            const QcBlockPos spot = QcPhysics::blockContaining(surface(*world, 2, 2));
            const int light = world->blockLight(spot);
            if (torches != (light >= 9) || light != QcLightVolume::blockLightAt(*world, spot)) {
                return -1;
            }

            QcMobManager mobs(m_content, 11);
            int count = 0;
            QSet<quint32> seen;
            for (int tick = 0; tick < 20 * 40; ++tick) {
                mobs.tick(*world, {{7, player, true}}, midnight, dt, [this] { return m_nextId++; });
                for (const QcCreature &mob : mobs.mobs()) {
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
        // Only zombies come at night here: the other night kinds would share the places by chance.
        for (const QcMobDefinition &other : m_content->mobs()) {
            if (other.name != zombie.name && other.spawnTime == QcMobDefinition::SpawnTime::Night && other.spawnWeight > 0.0) {
                QcMobDefinition without = other;
                without.spawnWeight = 0.0;
                anywhere->registerMob(without);
            }
        }

        QVERIFY(spawnedZombies(anywhere, nullptr) > 0);
        QCOMPARE(spawnedZombies(anywhere, "magma_block", 0), 0);
        QCOMPARE(spawnedZombies(anywhere, "cobweb", 1), 0);

        // Animals do not appear in cobwebs over their grass either.
        QVERIFY(populatedAnimals(nullptr) > 0);
        QCOMPARE(populatedAnimals("cobweb"), 0);
    }

    void animalsSpawnUnderTreeCrowns()
    {
        // A crown of leaves with room below: the groups stand on the grass under it, as without
        // the crown. Kinds that never come with a chunk (the mannequin may stand on anything) must
        // not make the search stop on the leaves.
        const int open = populatedAnimals(nullptr);
        QVERIFY(open > 0);
        QCOMPARE(populatedAnimals("oak_leaves", 4), open);
    }
};

QTEST_GUILESS_MAIN(TestMobs)
#include "test_mobs.moc"
