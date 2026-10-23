#include "common/physics/QcPhysics.h"
#include "common/physics/QcSpawnGround.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/mobs/QcMobUpkeep.h"
#include "server/spawning/QcMonsterCap.h"
#include "server/spawning/QcNaturalSpawner.h"
#include "server/spawning/QcSpawnLight.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QSet>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = 0.05;
constexpr double noon = QcDayCycle::dayLength * 0.25;
constexpr double midnight = QcDayCycle::dayLength * 0.75;

// Share of `draws` random tests the cell passes.
double passShare(QcSpawnLight::Cell cell, int draws)
{
    QRandomGenerator random(17);
    int passed = 0;
    for (int i = 0; i < draws; ++i) {
        passed += QcSpawnLight::darkEnough(cell, QcSpawnLight::draw(random));
    }
    return static_cast<double>(passed) / draws;
}

} // namespace

// The monster spawning of the original game (decision E-836): the light test, the cap per 289
// chunks, the distances and despawn timers, and the general ground rule.
class TestMonsterSpawning : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    quint32 m_nextId = 100;

    QcBlockId block(const QString &name) const
    {
        const std::optional<QcBlockId> id = m_content->blockId(u"qtcraft:"_s + name);
        if (!id) {
            QcTestUtil::failCase(u"unknown block %1"_s.arg(name));
            return qcAirBlockId;
        }
        return *id;
    }

    int type(const QString &name) const { return m_content->mobIndex(u"qtcraft:"_s + name).value_or(0); }

    std::unique_ptr<QcServerWorld> makeWorld(const std::shared_ptr<const QcContentRegistry> &content, int radius) const
    {
        auto world = std::make_unique<QcServerWorld>(content, std::make_unique<QcTestTerrainGenerator>(content));
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    static int top(const QcServerWorld &world, int x, int z)
    {
        const QcBlockPos column{x, 0, z};
        return world.chunk(column.chunk())->highestNonAir(column.localX(), column.localZ());
    }

    // Content in which only zombies spawn by themselves.
    std::shared_ptr<QcContentRegistry> onlyZombies() const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        for (const QcMobDefinition &definition : m_content->mobs()) {
            if (definition.name != u"qtcraft:zombie"_s && definition.spawnsNaturally()) {
                QcMobDefinition quiet = definition;
                quiet.spawnWeight = 0.0;
                quiet.extraSpawns.clear();
                content->registerMob(quiet);
            }
        }
        return content;
    }

    // A living player standing on the ground of column (x, z).
    static QcMobManager::PlayerView playerAt(const QcServerWorld &world, int x, int z, bool alive = true)
    {
        return {7 + static_cast<quint32>(std::abs(x) + std::abs(z)), {x + 0.5, static_cast<double>(top(world, x, z) + 1), z + 0.5}, alive};
    }

    // Distance from `at` to the nearest player with a body.
    static double nearestLiving(const QcVec3 &at, const QList<QcMobManager::PlayerView> &players)
    {
        double nearest = std::numeric_limits<double>::max();
        for (const QcMobManager::PlayerView &player : players) {
            nearest = player.alive ? std::min(nearest, (player.position - at).length()) : nearest;
        }
        return nearest;
    }

    // Zombies spawned within `seconds` at `time` around `players` (by default one at the origin);
    // each must have spawned 24-128 m from the nearest living one.
    QList<QcVec3> spawnedZombies(const std::shared_ptr<const QcContentRegistry> &content, QcServerWorld &world, double time,
                                 QList<QcMobManager::PlayerView> players = {}, int seconds = 30)
    {
        QcMobManager mobs(content, 9);
        if (players.isEmpty()) {
            players.append(playerAt(world, 0, 0));
        }
        QSet<quint32> seen;
        QList<QcVec3> spawns;
        for (int tick = 0; tick < 20 * seconds; ++tick) {
            mobs.tick(world, players, time, dt, [this] { return m_nextId++; });
            for (const QcCreature &mob : mobs.mobs()) {
                if (!seen.contains(mob.id) && mob.type == type(u"zombie"_s)) {
                    seen.insert(mob.id);
                    spawns.append(mob.motion.position);
                    const double distance = nearestLiving(mob.motion.position, players);
                    if (distance < QcNaturalSpawner::monsterRadiusMin || distance > QcNaturalSpawner::monsterRadiusMax) {
                        QcTestUtil::failCase(u"zombie spawned %1 m away"_s.arg(distance));
                    }
                }
            }
        }
        return spawns;
    }

    // A world in which only a stone square of 33 × 33 columns around (60, 0) carries monsters, the
    // rest of the ground being glass: within 24 m of that column, more than 40 m from the origin.
    std::unique_ptr<QcServerWorld> stoneSquareWorld(const std::shared_ptr<const QcContentRegistry> &content) const
    {
        auto world = makeWorld(content, 4);
        for (int x = -64; x < 80; ++x) {
            for (int z = -64; z < 80; ++z) {
                const bool square = std::abs(x - 60) <= 16 && std::abs(z) <= 16;
                world->setBlock({x, top(*world, x, z), z}, block(square ? u"stone"_s : u"glass"_s));
            }
        }
        return world;
    }

private slots:
    void skyDarkensByTimeAndWeather()
    {
        QCOMPARE(QcSpawnLight::skyDarkening(noon, 0.0f, 0.0f), 0);
        QCOMPARE(QcSpawnLight::skyDarkening(midnight, 0.0f, 0.0f), QcSpawnLight::nightDarkening);
        // Rain takes 5/16 of the sun: 11 × 5/16 = 3.4 levels.
        QCOMPARE(QcSpawnLight::skyDarkening(noon, 1.0f, 0.0f), 3);
        // A full thunderstorm darkens day and night alike, as in the original.
        QCOMPARE(QcSpawnLight::skyDarkening(noon, 1.0f, 1.0f), QcSpawnLight::thunderDarkening);
        QCOMPARE(QcSpawnLight::skyDarkening(midnight, 1.0f, 1.0f), QcSpawnLight::thunderDarkening);
        // Dusk lies in between and darkens step by step.
        int previous = 0;
        for (int step = 25; step <= 75; ++step) {
            const int darkening = QcSpawnLight::skyDarkening(step / 100.0 * QcDayCycle::dayLength, 0.0f, 0.0f);
            QVERIFY(darkening >= previous);
            previous = darkening;
        }
    }

    void blockLightKeepsMonstersAway()
    {
        // Block light above the rule's limit fails whatever the draws, in a cave as at night.
        for (int sky = 0; sky < QcSpawnLight::skyChance; ++sky) {
            for (int threshold = 0; threshold <= QcSpawnLight::maxThreshold; ++threshold) {
                QVERIFY(!QcSpawnLight::darkEnough({0, 1, 11, 0}, {sky, threshold}));
                QVERIFY(QcSpawnLight::darkEnough({0, 0, 11, 0}, {sky, threshold}));
                // The open sky at noon is too bright for every draw.
                QVERIFY(!QcSpawnLight::darkEnough({15, 0, 0, 0}, {sky, threshold}));
            }
        }
        // A rule may allow some block light (the slime's swamp rule keeps the default 0).
        QVERIFY(QcSpawnLight::darkEnough({0, 3, 11, 7}, {0, 3}));
        QVERIFY(!QcSpawnLight::darkEnough({0, 3, 11, 7}, {0, 2}));
    }

    void skyLightThresholdIsDrawnFromZeroToSeven()
    {
        QRandomGenerator random(5);
        QList<int> thresholds(QcSpawnLight::maxThreshold + 1, 0);
        QList<int> skies(QcSpawnLight::skyChance, 0);
        for (int i = 0; i < 32000; ++i) {
            const QcSpawnLight::Draws draws = QcSpawnLight::draw(random);
            QVERIFY(draws.threshold >= 0 && draws.threshold <= QcSpawnLight::maxThreshold);
            QVERIFY(draws.sky >= 0 && draws.sky < QcSpawnLight::skyChance);
            thresholds[draws.threshold] += 1;
            skies[draws.sky] += 1;
        }
        for (const int count : std::as_const(thresholds)) {
            QVERIFY2(std::abs(count - 4000) < 300, qPrintable(QString::number(count)));
        }
        for (const int count : std::as_const(skies)) {
            QVERIFY2(std::abs(count - 1000) < 150, qPrintable(QString::number(count)));
        }

        // A brightness b passes (8 - b) of 8 thresholds: no sky light, block light b allowed.
        for (int brightness = 0; brightness <= 9; ++brightness) {
            const double expected = std::max(0, QcSpawnLight::maxThreshold + 1 - brightness) / 8.0;
            const double share = passShare({0, brightness, 0, 15}, 20000);
            QVERIFY2(std::abs(share - expected) < 0.015, qPrintable(u"brightness %1: %2"_s.arg(brightness).arg(share)));
        }
        // The open sky at midnight: 17 of 32 sky draws and 4 of 8 thresholds (brightness 15 - 11).
        const double night = passShare({15, 0, QcSpawnLight::nightDarkening, 0}, 40000);
        QVERIFY2(std::abs(night - 17.0 / 32.0 * 4.0 / 8.0) < 0.012, qPrintable(QString::number(night)));
        // Under a roof that lets 8 levels of sky light through: only the sky draw decides at night.
        const double shade = passShare({8, 0, QcSpawnLight::nightDarkening, 0}, 40000);
        QVERIFY2(std::abs(shade - 24.0 / 32.0) < 0.012, qPrintable(QString::number(shade)));
    }

    void capFollowsTheLoadedChunksOfOnePlayer()
    {
        // All 289 chunks loaded: 70; a smaller loaded world gives its share.
        QcMonsterCap full(70, {{0, 0}}, [](QcChunkPos) { return true; });
        QCOMPARE(full.worldCap(), 70);
        QCOMPARE(full.roomIn({8, -8}), 70);
        QCOMPARE(full.roomIn({9, 0}), 0);
        for (int i = 0; i < 69; ++i) {
            full.add({i % 17 - 8, 0});
        }
        QCOMPARE(full.roomIn({0, 0}), 1);
        full.add({0, 0});
        QVERIFY(!full.hasRoom());
        QCOMPARE(full.roomIn({0, 0}), 0);

        const QcMonsterCap small(70, {{0, 0}}, [](QcChunkPos chunk) { return std::abs(chunk.x) <= 4 && std::abs(chunk.z) <= 4; });
        QCOMPARE(small.worldCap(), 70 * 81 / 289);
    }

    void capIsSharedByPlayersCloseTogether()
    {
        const auto everything = [](QcChunkPos) { return true; };
        // Far apart: two full shares, and one crowded area leaves the other its room.
        QcMonsterCap apart(70, {{0, 0}, {100, 0}}, everything);
        QCOMPARE(apart.worldCap(), 140);
        for (int i = 0; i < 70; ++i) {
            apart.add({0, 0});
        }
        QCOMPARE(apart.roomIn({0, 0}), 0);
        QCOMPARE(apart.roomIn({100, 0}), 70);
        QCOMPARE(apart.countAround(0), 70);
        QCOMPARE(apart.countAround(1), 0);

        // In the same chunk: one share for both.
        const QcMonsterCap together(70, {{0, 0}, {0, 0}}, everything);
        QCOMPARE(together.worldCap(), 70);

        // Eight chunks apart: the areas overlap in 9 × 17 chunks, 425 chunks in all.
        QcMonsterCap overlapping(70, {{0, 0}, {8, 0}}, everything);
        QCOMPARE(overlapping.worldCap(), 70 * 425 / 289);
        for (int i = 0; i < 70; ++i) {
            overlapping.add({4, 0});
        }
        // Both areas hold the monsters of the overlap: neither has room left, beyond the overlap
        // only the world's remaining room counts for chunks of the other area.
        QCOMPARE(overlapping.countAround(0), 70);
        QCOMPARE(overlapping.countAround(1), 70);
        QCOMPARE(overlapping.roomIn({16, 0}), 0);
        QCOMPARE(overlapping.roomIn({-8, 0}), 0);
    }

    void spawnerCapCountsMonstersOfLivingPlayers()
    {
        auto world = makeWorld(m_content, 4);
        QcMobManager mobs(m_content, 3);
        const QcVec3 player{0.5, static_cast<double>(top(*world, 0, 0) + 1), 0.5};
        mobs.spawn(type(u"zombie"_s), {30.5, static_cast<double>(top(*world, 30, 0) + 1), 0.5}, m_nextId++);
        mobs.spawn(type(u"skeleton"_s), {-30.5, static_cast<double>(top(*world, -30, 0) + 1), 0.5}, m_nextId++);
        // Peaceful creatures and creatures of spawners take no room from the monsters.
        mobs.spawn(type(u"pig"_s), {10.5, static_cast<double>(top(*world, 10, 0) + 1), 0.5}, m_nextId++, false, true);
        mobs.spawn(type(u"zombie"_s), {20.5, static_cast<double>(top(*world, 20, 0) + 1), 0.5}, m_nextId++);
        mobs.editableMobs().last().spawner = QcBlockPos{20, 10, 0};
        // Nor do kept monsters (named ones, those placed with the world).
        mobs.spawn(type(u"zombie"_s), {40.5, static_cast<double>(top(*world, 40, 0) + 1), 0.5}, m_nextId++, false, true);

        const QcMonsterCap cap = mobs.spawner().monsterCap(*world, {{7, player, true}});
        QCOMPARE(cap.worldCap(), 70 * 81 / 289);
        QCOMPARE(cap.count(), 2);
        // A spectator spans no area.
        const QcMonsterCap watching = mobs.spawner().monsterCap(*world, {{7, player, false}});
        QCOMPARE(watching.worldCap(), 0);
    }

    void monstersFarAwayDespawnAtOnce()
    {
        auto world = makeWorld(m_content, 9);
        QcMobManager mobs(m_content, 4);
        mobs.setNaturalSpawning(false);
        const QcVec3 player{0.5, static_cast<double>(top(*world, 0, 0) + 1), 0.5};
        const quint32 near = mobs.spawn(type(u"spider"_s), {120.5, static_cast<double>(top(*world, 120, 0) + 1), 0.5}, m_nextId++);
        const quint32 far = mobs.spawn(type(u"spider"_s), {135.5, static_cast<double>(top(*world, 135, 0) + 1), 0.5}, m_nextId++);
        mobs.tick(*world, {{7, player, true}}, midnight, dt, [this] { return m_nextId++; });
        QVERIFY(mobs.find(near));
        QVERIFY(!mobs.find(far));
    }

    void idleCreaturesDespawnByChance()
    {
        // The rule itself: idle beyond 600 ticks and nobody within 32 m.
        QcCreature mob;
        mob.motion.position = {40.0, 0.0, 0.0};
        const QcMobDefinition &spider = m_content->mobs()[type(u"spider"_s)];
        const QList<QcMobManager::PlayerView> players{{7, {0.0, 0.0, 0.0}, true}};
        const auto fate = [&] { return QcMobUpkeep::upkeepOf(mob, spider, players, true).fate; };
        // The tick counts: 600 idle ticks before it, 601 after.
        mob.idleTicks = QcMobUpkeep::idleTicks - 1;
        QCOMPARE(fate(), QcMobUpkeep::Fate::Stays);
        mob.idleTicks = QcMobUpkeep::idleTicks;
        QCOMPARE(fate(), QcMobUpkeep::Fate::MayDespawn);
        mob.persistent = true;
        QCOMPARE(fate(), QcMobUpkeep::Fate::Stays);
        mob.persistent = false;
        // A guardian keeps its zone; beyond 128 m it goes like the others.
        mob.guards = QcGuardPost{};
        QCOMPARE(fate(), QcMobUpkeep::Fate::Stays);
        mob.motion.position = {130.0, 0.0, 0.0};
        QCOMPARE(fate(), QcMobUpkeep::Fate::Despawns);
        mob.guards.reset();
        // The timer stops one above the limit, however long nobody comes.
        mob.motion.position = {40.0, 0.0, 0.0};
        mob.idleTicks = QcMobUpkeep::idleTicks + 1;
        QCOMPARE(QcMobUpkeep::upkeepOf(mob, spider, players, true).idleTicks, QcMobUpkeep::idleTicks + 1);
        mob.idleTicks = std::numeric_limits<int>::max();
        QCOMPARE(QcMobUpkeep::upkeepOf(mob, spider, players, true).idleTicks, QcMobUpkeep::idleTicks + 1);
        mob.motion.position = {30.0, 0.0, 0.0};
        QCOMPARE(fate(), QcMobUpkeep::Fate::Stays);
        QCOMPARE(QcMobUpkeep::upkeepOf(mob, spider, players, true).idleTicks, 0);
        QCOMPARE(QcMobUpkeep::upkeepOf(mob, spider, {}, true), (QcMobUpkeep::Upkeep{QcMobUpkeep::Fate::Stays, 0}));

        // Ticked by hand: 200 still spiders 40-120 m away stay 30 s, then about 1 in 800 go per
        // tick (63 % after 800 more ticks); one 10 m from the player stays.
        auto world = makeWorld(m_content, 8);
        QcMobManager mobs(m_content, 6);
        mobs.setNaturalSpawning(false);
        const QcVec3 player{0.5, static_cast<double>(top(*world, 0, 0) + 1), 0.5};
        for (int i = 0; i < 200; ++i) {
            const double angle = i * 0.7;
            const double radius = 40.0 + (i % 9) * 10.0;
            const int x = static_cast<int>(std::floor(std::cos(angle) * radius));
            const int z = static_cast<int>(std::floor(std::sin(angle) * radius));
            mobs.spawn(type(u"spider"_s), {x + 0.5, static_cast<double>(top(*world, x, z) + 1), z + 0.5}, m_nextId++);
            mobs.editableMobs().last().noAi = true;
        }
        const quint32 close = mobs.spawn(type(u"spider"_s), {10.5, static_cast<double>(top(*world, 10, 0) + 1), 0.5}, m_nextId++);
        mobs.editableMobs().last().noAi = true;

        const auto tickFor = [&](int ticks) {
            for (int tick = 0; tick < ticks; ++tick) {
                mobs.tick(*world, {{7, player, true}}, midnight, dt, [this] { return m_nextId++; });
            }
        };
        tickFor(QcMobUpkeep::idleTicks);
        QCOMPARE(mobs.mobs().size(), qsizetype(201));
        tickFor(QcMobUpkeep::idleChance);
        const qsizetype gone = 201 - mobs.mobs().size();
        QVERIFY2(gone > 100 && gone < 155, qPrintable(QString::number(gone)));
        QVERIFY(mobs.find(close));
    }

    void struckCreaturesAreNotIdle()
    {
        // A still spider 40 m away counts idle ticks; a blow sets them back to 0.
        auto world = makeWorld(m_content, 4);
        QcMobManager mobs(m_content, 6);
        mobs.setNaturalSpawning(false);
        const quint32 spider = mobs.spawn(type(u"spider"_s), {40.5, static_cast<double>(top(*world, 40, 0) + 1), 0.5}, m_nextId++);
        mobs.editableMobs().last().noAi = true;
        for (int tick = 0; tick < 100; ++tick) {
            mobs.tick(*world, {playerAt(*world, 0, 0)}, noon, dt, [this] { return m_nextId++; });
        }
        QCOMPARE(mobs.find(spider)->idleTicks, 100);
        // It survives the blow: no loot.
        QVERIFY(!mobs.damage({.creature = spider, .damage = 1.0, .from = {39.5, 70.0, 0.5}}));
        QCOMPARE(mobs.find(spider)->idleTicks, 0);
    }

    void spectatorsAndTheDeadKeepNobody()
    {
        // A spectator or a dead player next to a creature neither resets its timer nor keeps it
        // from vanishing beyond 128 m of the living; with nobody living around nobody vanishes.
        QcCreature mob;
        mob.motion.position = {200.0, 0.0, 0.0};
        mob.idleTicks = 5;
        const QcMobDefinition &spider = m_content->mobs()[type(u"spider"_s)];
        const QcMobManager::PlayerView living{7, {0.0, 0.0, 0.0}, true};
        const QcMobManager::PlayerView watching{8, {195.0, 0.0, 0.0}, false};
        QCOMPARE(QcMobUpkeep::upkeepOf(mob, spider, {living, watching}, true), (QcMobUpkeep::Upkeep{QcMobUpkeep::Fate::Despawns, 6}));
        QCOMPARE(QcMobUpkeep::upkeepOf(mob, spider, {watching}, true), (QcMobUpkeep::Upkeep{QcMobUpkeep::Fate::Stays, 0}));
    }

    void monstersKeepAwayFromEveryLivingPlayer()
    {
        // Only the stone square around (60, 0) carries zombies. A spectator there keeps none away;
        // a living player there keeps every one away (24 m), though it spans an area of its own.
        const std::shared_ptr<QcContentRegistry> content = onlyZombies();
        auto watched = stoneSquareWorld(content);
        const QList<QcVec3> nearSpectator =
            spawnedZombies(content, *watched, midnight, {playerAt(*watched, 0, 0), playerAt(*watched, 60, 0, false)}, 60);
        QVERIFY(std::ranges::any_of(nearSpectator, [&](const QcVec3 &at) { return (at - playerAt(*watched, 60, 0).position).length() < 24.0; }));

        auto guarded = stoneSquareWorld(content);
        QVERIFY(spawnedZombies(content, *guarded, midnight, {playerAt(*guarded, 0, 0), playerAt(*guarded, 60, 0)}, 60).isEmpty());
    }

    void capFollowsChunksLoadedLater()
    {
        // Nine chunks loaded: room for 70 × 9 / 289 = 2 monsters. When 81 are loaded the cap grows
        // within a second, though the player stays in its chunk.
        const std::shared_ptr<QcContentRegistry> content = onlyZombies();
        auto world = makeWorld(content, 1);
        QcMobManager mobs(content, 9);
        const QList<QcMobManager::PlayerView> players{playerAt(*world, 0, 0)};
        const auto zombiesAfter = [&](int seconds) {
            for (int tick = 0; tick < 20 * seconds; ++tick) {
                mobs.tick(*world, players, midnight, dt, [this] { return m_nextId++; });
            }
            return std::ranges::count(mobs.mobs(), type(u"zombie"_s), &QcCreature::type);
        };
        QVERIFY(zombiesAfter(30) <= 2);
        for (int x = -4; x <= 4; ++x) {
            for (int z = -4; z <= 4; ++z) {
                world->loadChunk({x, z});
            }
        }
        QVERIFY(zombiesAfter(30) > 2);
    }

    void groundRuleNeedsAnOpaqueFullTop()
    {
        const QcContentRegistry &content = *m_content;
        for (const QString &name : {u"stone"_s, u"grass_block"_s, u"dirt"_s, u"sand"_s, u"packed_ice"_s, u"oak_planks"_s}) {
            QVERIFY2(QcSpawnGround::carries(content, block(name)), qPrintable(name));
        }
        for (const QString &name : {u"glass"_s, u"oak_leaves"_s, u"ice"_s, u"magma_block"_s, u"dirt_path"_s, u"farmland"_s, u"snow_layer"_s,
                                    u"water"_s, u"tall_grass"_s}) {
            QVERIFY2(!QcSpawnGround::carries(content, block(name)), qPrintable(name));
        }

        // Slabs: the lower one does not carry, the upper and the double one do.
        const auto slab = std::ranges::find_if(content.blocks(), [](const QcBlockDefinition &definition) {
            return definition.name.endsWith(u"_slab"_s) && !definition.name.contains(u"vertical"_s);
        });
        QVERIFY(slab != content.blocks().end());
        const QcBlockId bottom = content.withProperty(*content.blockId(slab->name), u"type"_s, u"bottom"_s);
        QVERIFY(!QcSpawnGround::carries(content, bottom));
        QVERIFY(QcSpawnGround::carries(content, content.withProperty(bottom, u"type"_s, u"top"_s)));
        QVERIFY(QcSpawnGround::carries(content, content.withProperty(bottom, u"type"_s, u"double"_s)));
        QCOMPARE(QcSpawnGround::carryingOf(content, content.state(bottom).type), QcSpawnGround::Carrying::Some);
        QCOMPARE(QcSpawnGround::carryingOf(content, content.state(block(u"stone"_s)).type), QcSpawnGround::Carrying::All);
        QCOMPARE(QcSpawnGround::carryingOf(content, content.state(block(u"glass"_s)).type), QcSpawnGround::Carrying::None);

        // Feet and head: free cells, plants; no liquids, rails, cobwebs or solid blocks.
        for (const QString &name : {u"air"_s, u"tall_grass"_s, u"torch"_s}) {
            QVERIFY2(QcSpawnGround::leavesRoom(content, block(name)), qPrintable(name));
        }
        for (const QString &name : {u"water"_s, u"rail"_s, u"cobweb"_s, u"stone"_s, u"glass"_s}) {
            QVERIFY2(!QcSpawnGround::leavesRoom(content, block(name)), qPrintable(name));
        }
    }

    void noMonstersOnGlassLeavesOrLowerSlabs()
    {
        // The whole loaded ground covered by a block: zombies come at night only where it carries.
        const std::shared_ptr<QcContentRegistry> content = onlyZombies();
        const auto zombiesOn = [&](QcBlockId cover) {
            auto world = makeWorld(content, 4);
            for (int x = -64; x < 80; ++x) {
                for (int z = -64; z < 80; ++z) {
                    world->setBlock({x, top(*world, x, z), z}, cover);
                }
            }
            return spawnedZombies(content, *world, midnight).size();
        };

        QVERIFY(zombiesOn(block(u"stone"_s)) > 0);
        QCOMPARE(zombiesOn(block(u"glass"_s)), qsizetype(0));
        QCOMPARE(zombiesOn(block(u"oak_leaves"_s)), qsizetype(0));
        QCOMPARE(zombiesOn(block(u"ice"_s)), qsizetype(0));
        const auto slab = std::ranges::find_if(content->blocks(), [](const QcBlockDefinition &definition) {
            return definition.name.endsWith(u"_slab"_s) && !definition.name.contains(u"vertical"_s);
        });
        const QcBlockId bottom = content->withProperty(*content->blockId(slab->name), u"type"_s, u"bottom"_s);
        QCOMPARE(zombiesOn(bottom), qsizetype(0));
        QVERIFY(zombiesOn(content->withProperty(bottom, u"type"_s, u"top"_s)) > 0);
    }

    void monstersSpawnByDayUnderARoof()
    {
        // The open test world by day: none. A stone roof three blocks over the ground (a dark forest,
        // a covered hall): zombies under it at noon, out of the sun.
        const std::shared_ptr<QcContentRegistry> content = onlyZombies();
        auto open = makeWorld(content, 4);
        QVERIFY(spawnedZombies(content, *open, noon).isEmpty());

        auto covered = makeWorld(content, 4);
        const QcBlockId stone = block(u"stone"_s);
        for (int x = -64; x < 80; ++x) {
            for (int z = -64; z < 80; ++z) {
                covered->setBlock({x, top(*covered, x, z) + 4, z}, stone);
            }
        }
        const QList<QcVec3> spawns = spawnedZombies(content, *covered, noon);
        QVERIFY(!spawns.isEmpty());
        for (const QcVec3 &at : spawns) {
            const QcBlockPos feet = QcPhysics::blockContaining(at);
            QVERIFY2(covered->skyLight(feet) <= QcSpawnLight::maxThreshold, qPrintable(u"sky light %1"_s.arg(covered->skyLight(feet))));
            QVERIFY(top(*covered, feet.x, feet.z) > feet.y);
        }
    }

    void monstersSpawnByDayInCaves()
    {
        // A closed cave three blocks high, eight blocks of rock above it: zombies there at noon.
        const std::shared_ptr<QcContentRegistry> content = onlyZombies();
        auto world = makeWorld(content, 4);
        const int floor = top(*world, 0, 0) - 14;
        for (int x = -64; x < 80; ++x) {
            for (int z = -64; z < 80; ++z) {
                for (int y = floor + 1; y <= floor + 3; ++y) {
                    world->setBlock({x, y, z}, qcAirBlockId);
                }
            }
        }
        const QList<QcVec3> spawns = spawnedZombies(content, *world, noon);
        QVERIFY(!spawns.isEmpty());
        for (const QcVec3 &at : spawns) {
            QCOMPARE(QcPhysics::blockContaining(at).y, floor + 1);
        }
    }
};

QTEST_GUILESS_MAIN(TestMonsterSpawning)
#include "test_monster_spawning.moc"
