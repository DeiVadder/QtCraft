#include "common/content/parser/QcContentParser.h"
#include "common/content/registry/QcSpawnerContent.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/spawning/QcNaturalSpawner.h"
#include "server/spawning/QcSpawnAreas.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QJsonDocument>
#include <QSet>
#include <QTest>

#include <atomic>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = 0.05;
constexpr double noon = QcDayCycle::dayLength * 0.25;
constexpr double midnight = QcDayCycle::dayLength * 0.75;
// Ticks of one hour of game time.
constexpr int hourTicks = 72000;

// The test terrain with fixed spawn areas (QcWorldGenerator::structureSitesNear); they never change,
// so generation threads may read them.
class AreaTerrain : public QcTestTerrainGenerator
{
public:
    AreaTerrain(std::shared_ptr<const QcContentRegistry> content, QList<QcSpawnArea> areas)
        : QcTestTerrainGenerator(std::move(content))
        , m_areas(std::move(areas))
    {
    }

    QcStructureSites structureSitesNear(QcBlockPos from, int reach) const override
    {
        Q_UNUSED(from);
        Q_UNUSED(reach);
        return {{}, m_areas};
    }

    // Unplanned: the tick thread is told to plan in the background (plannedStructureSitesNear).
    void setPlanned(bool planned) { m_planned = planned; }

    std::optional<QcStructureSites> plannedStructureSitesNear(QcBlockPos from, int reach) const override
    {
        return m_planned ? std::optional<QcStructureSites>(structureSitesNear(from, reach)) : std::nullopt;
    }

private:
    const QList<QcSpawnArea> m_areas;
    std::atomic<bool> m_planned{true};
};

// A creature as it appeared: its kind and its feet.
struct Appearance
{
    int type = 0;
    QcVec3 feet;
};

// The creatures seen so far, each once, in the order they appeared.
struct Watch
{
    QSet<quint32> seen;
    QList<Appearance> appeared;

    void look(const QcMobManager &mobs)
    {
        for (const QcCreature &mob : mobs.mobs()) {
            if (!seen.contains(mob.id)) {
                seen.insert(mob.id);
                appeared.append({mob.type, mob.motion.position});
            }
        }
    }
};

} // namespace

// Witch huts as spawn areas of the original game (decision E-871): in the footprint of a hut over
// the whole height of the world the natural monster spawns bring forth only witches, under the
// monsters' light, distance and cap rules; the sand golem keeps its guardian zone. Ticks are stepped
// by hand.
class TestWitchHutSpawns : public QObject
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

    // Content in which only zombies spawn by themselves outside spawn areas.
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

    static QcSpawnArea witchArea(QcBlockPos min, QcBlockPos max)
    {
        return {{min.x, 0, min.z}, {max.x, QcWorld::worldHeight - 1, max.z}, {u"qtcraft:witch"_s}};
    }

    std::unique_ptr<QcServerWorld> makeWorld(const std::shared_ptr<const QcContentRegistry> &content, int radius,
                                             const QList<QcSpawnArea> &areas) const
    {
        auto world = std::make_unique<QcServerWorld>(content, std::make_unique<AreaTerrain>(content, areas));
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

    // Replaces the top block of every column of the loaded square of `radius` chunks.
    static void coverGround(QcServerWorld &world, int radius, QcBlockId cover)
    {
        const int edge = (radius + 1) * QcWorld::chunkSize;
        for (int x = -edge + QcWorld::chunkSize; x < edge; ++x) {
            for (int z = -edge + QcWorld::chunkSize; z < edge; ++z) {
                world.setBlock({x, top(world, x, z), z}, cover);
            }
        }
    }

    // A layer of `fill` at height `y` over the columns min..max.
    static void layer(QcServerWorld &world, QcBlockPos min, QcBlockPos max, int y, QcBlockId fill)
    {
        for (int x = min.x; x <= max.x; ++x) {
            for (int z = min.z; z <= max.z; ++z) {
                world.setBlock({x, y, z}, fill);
            }
        }
    }

    // The columns min..max made level: stone up to `y`, air above it up to the old top.
    static void flatten(QcServerWorld &world, QcBlockPos min, QcBlockPos max, int y, QcBlockId stone)
    {
        for (int x = min.x; x <= max.x; ++x) {
            for (int z = min.z; z <= max.z; ++z) {
                const int old = top(world, x, z);
                for (int cell = std::min(old, y); cell <= std::max(old, y) + 1; ++cell) {
                    world.setBlock({x, cell, z}, cell <= y ? stone : qcAirBlockId);
                }
            }
        }
    }

    // A living player standing on the ground of column (x, z).
    static QcMobManager::PlayerView playerAt(const QcServerWorld &world, int x, int z)
    {
        return {7, {x + 0.5, static_cast<double>(top(world, x, z) + 1), z + 0.5}, true};
    }

    // The creatures that appear within `ticks` at `time`, each removed in the tick it appeared (a
    // farm that kills at once).
    QList<Appearance> run(QcMobManager &mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players, double time, int ticks)
    {
        Watch watch;
        for (int tick = 0; tick < ticks; ++tick) {
            mobs.tick(world, players, time, dt, [this] { return m_nextId++; });
            watch.look(mobs);
            mobs.removeAll();
        }
        return watch.appeared;
    }

    // The creatures that appear within `ticks` at midnight; they stay.
    QList<Appearance> keep(QcMobManager &mobs, QcServerWorld &world, const QList<QcMobManager::PlayerView> &players, int ticks)
    {
        Watch watch;
        for (int tick = 0; tick < ticks; ++tick) {
            mobs.tick(world, players, midnight, dt, [this] { return m_nextId++; });
            watch.look(mobs);
        }
        return watch.appeared;
    }

    QList<Appearance> witchesOf(const QList<Appearance> &appeared) const
    {
        QList<Appearance> witches;
        for (const Appearance &appearance : appeared) {
            if (appearance.type == type(u"witch"_s)) {
                witches.append(appearance);
            }
        }
        return witches;
    }

    static bool inside(const QcSpawnArea &area, const QcVec3 &feet)
    {
        const QcBlockPos cell = QcPhysics::blockContaining(feet);
        return area.containsColumn(cell.x, cell.z);
    }

    // The witch hut of the base game with its north west bottom corner at `origin`, unturned.
    QcStructurePlacement hutAt(QcBlockPos origin) const
    {
        const QcStructure::Loaded hut = QcStructure::fromDefinition(*m_content->worldgen().structure(u"qtcraft:witch_hut"_s), *m_content);
        if (!hut) {
            QcTestUtil::failCase(u"the witch hut does not load"_s);
            return {std::make_shared<const QcStructure>(u"none"_s, 1, 1, 1), origin, 0, *m_content};
        }
        return {std::make_shared<const QcStructure>(*hut), origin, 0, *m_content};
    }

    // Writes the cells of a placed structure into the world.
    static void build(QcServerWorld &world, const QcStructurePlacement &placement)
    {
        const QcStructure &structure = placement.structure();
        for (int x = 0; x < structure.sizeX(); ++x) {
            for (int y = 0; y < structure.sizeY(); ++y) {
                for (int z = 0; z < structure.sizeZ(); ++z) {
                    if (const std::optional<QcBlockId> state = placement.worldBlock({x, y, z})) {
                        world.setBlock(placement.toWorld(QcBlockPos{x, y, z}), *state);
                    }
                }
            }
        }
    }

    // Witches a hut yields in one game hour with a player `distance` blocks west of it, every witch
    // taken away at once and no other ground for monsters (glass); `storeys` more dark floors of
    // stone lie in its footprint above the roof, three blocks apart.
    int hourlyYield(int storeys, int distance)
    {
        const QcBlockPos corner{40, 0, -4};
        const QcStructurePlacement hut = hutAt(corner.offset(0, QcTestTerrainGenerator::surfaceHeight(corner.x, corner.z), 0));
        const QcSpawnArea area = QcSpawnArea::of(hut).value_or(QcSpawnArea{});
        auto world = makeWorld(m_content, 4, {area});
        coverGround(*world, 4, block(u"glass"_s));
        build(*world, hut);
        const int roof = hut.origin().y + hut.structure().sizeY();
        for (int storey = 1; storey <= storeys; ++storey) {
            layer(*world, area.min, area.max, roof + 3 * storey, block(u"stone"_s));
        }

        QcMobManager mobs(m_content, 9);
        const double z = static_cast<double>(area.min.z + area.max.z + 1) / 2.0;
        const QList<QcMobManager::PlayerView> players{{7, {area.min.x - distance + 0.5, static_cast<double>(hut.origin().y), z}, true}};
        int witches = 0;
        for (int tick = 0; tick < hourTicks; ++tick) {
            // The day runs on: six days and nights in an hour.
            const double time = std::fmod(QcDayCycle::startTime + tick * dt, QcDayCycle::dayLength);
            mobs.tick(*world, players, time, dt, [this] { return m_nextId++; });
            for (const QcCreature &mob : mobs.mobs()) {
                witches += mob.type == type(u"witch"_s) && inside(area, mob.motion.position);
            }
            mobs.removeAll();
        }
        return witches;
    }

private slots:
    void witchesOnlyInTheFootprintOverTheFullHeight()
    {
        // The whole ground carries zombies, and so do four platforms of stone over the area and two
        // blocks around it. In the area only witches come, on the ground and on every platform.
        const std::shared_ptr<QcContentRegistry> content = onlyZombies();
        const QcSpawnArea area = witchArea({48, 0, -16}, {79, 0, 15});
        auto world = makeWorld(content, 4, {area});
        coverGround(*world, 4, block(u"stone"_s));
        const int ground = top(*world, 60, 0);
        for (int storey = 1; storey <= 4; ++storey) {
            layer(*world, area.min.offset(-2, 0, -2), area.max.offset(2, 0, 2), ground + 5 * storey, block(u"stone"_s));
        }

        QcMobManager mobs(content, 9);
        const QList<Appearance> appeared = run(mobs, *world, {playerAt(*world, 0, 0)}, midnight, 12000);
        const QList<Appearance> witches = witchesOf(appeared);
        QSet<int> heights;
        for (const Appearance &witch : witches) {
            QVERIFY2(inside(area, witch.feet), qPrintable(u"witch at %1 %2"_s.arg(witch.feet.x).arg(witch.feet.z)));
            heights.insert(QcPhysics::blockContaining(witch.feet).y);
        }
        int zombies = 0;
        for (const Appearance &appearance : appeared) {
            QVERIFY(appearance.type == type(u"witch"_s) || appearance.type == type(u"zombie"_s));
            if (appearance.type == type(u"zombie"_s)) {
                QVERIFY2(!inside(area, appearance.feet), qPrintable(u"zombie at %1 %2"_s.arg(appearance.feet.x).arg(appearance.feet.z)));
                ++zombies;
            }
        }
        qInfo().noquote() << u"%1 witches at %2 heights, %3 zombies"_s.arg(witches.size()).arg(heights.size()).arg(zombies);
        QVERIFY(zombies > 0);
        QVERIFY(witches.size() >= 10);
        // The ground and the platforms, the highest one too (its top under the open night sky).
        QVERIFY(heights.size() >= 4);
        QVERIFY(heights.contains(ground + 21));
    }

    void witchesCountTowardsTheMonsterCap()
    {
        // Nine chunks loaded: room for 70 × 9 / 289 = 2 monsters; only the area carries any.
        const QcSpawnArea area = witchArea({24, 0, -16}, {31, 0, 15});
        auto world = makeWorld(m_content, 1, {area});
        coverGround(*world, 1, block(u"glass"_s));
        for (int x = area.min.x; x <= area.max.x; ++x) {
            for (int z = area.min.z; z <= area.max.z; ++z) {
                world->setBlock({x, top(*world, x, z), z}, block(u"stone"_s));
            }
        }
        const QList<QcMobManager::PlayerView> players{playerAt(*world, 0, 0)};

        QcMobManager mobs(m_content, 9);
        int most = 0;
        for (int tick = 0; tick < 20; ++tick) {
            keep(mobs, *world, players, 600);
            const auto witches = std::ranges::count(mobs.mobs(), type(u"witch"_s), &QcCreature::type);
            most = std::max(most, static_cast<int>(witches));
            // Only witches are monsters here; fireflies may come as well, which are none.
            QCOMPARE(mobs.spawner().monsterCap(*world, players).count(), static_cast<int>(witches));
        }
        QCOMPARE(mobs.spawner().monsterCap(*world, players).worldCap(), 2);
        QCOMPARE(most, 2);

        // Two zombies near the player fill the cap: no witch comes.
        QcMobManager full(m_content, 9);
        full.spawn(type(u"zombie"_s), playerAt(*world, 6, 6).position, m_nextId++);
        full.spawn(type(u"zombie"_s), playerAt(*world, -6, 6).position, m_nextId++);
        QVERIFY(witchesOf(keep(full, *world, players, 6000)).isEmpty());
    }

    void lightRulesHold()
    {
        const QcSpawnArea area = witchArea({48, 0, -16}, {63, 0, 15});
        const auto witchesWith = [&](double time, const std::function<void(QcServerWorld &, int ground)> &build) {
            auto world = makeWorld(m_content, 4, {area});
            coverGround(*world, 4, block(u"glass"_s));
            const int ground = top(*world, 56, 0);
            flatten(*world, area.min.offset(-4, 0, -4), area.max.offset(4, 0, 4), ground, block(u"stone"_s));
            build(*world, ground);
            QcMobManager mobs(m_content, 9);
            return witchesOf(run(mobs, *world, {playerAt(*world, 0, 0)}, time, 6000)).size();
        };
        const auto nothing = [](QcServerWorld &, int) {};

        // Under the open sky: at night, never at noon; under a roof also at noon.
        QVERIFY(witchesWith(midnight, nothing) > 0);
        QCOMPARE(witchesWith(noon, nothing), qsizetype{0});
        QVERIFY(witchesWith(noon, [&](QcServerWorld &world, int ground) {
            layer(world, area.min.offset(-4, 0, -4), area.max.offset(4, 0, 4), ground + 4, block(u"stone"_s));
        }) > 0);

        // Torches give every cell above the floor block light: none at night.
        QCOMPARE(witchesWith(midnight, [&](QcServerWorld &world, int ground) {
            for (int x = area.min.x + 3; x <= area.max.x; x += 6) {
                for (int z = area.min.z + 3; z <= area.max.z; z += 6) {
                    world.setBlock({x, ground + 1, z}, block(u"torch"_s));
                }
            }
            for (int x = area.min.x; x <= area.max.x; ++x) {
                for (int z = area.min.z; z <= area.max.z; ++z) {
                    QVERIFY2(world.blockLight({x, ground + 1, z}) > 0, "a dark cell is left");
                }
            }
        }), qsizetype{0});
    }

    void witchesKeepTheMonsterDistance()
    {
        // The player stands in a long area: witches only 24 m from it and further.
        const std::shared_ptr<QcContentRegistry> content = onlyZombies();
        const QcSpawnArea area = witchArea({-48, 0, -8}, {47, 0, 7});
        auto world = makeWorld(content, 4, {area});
        coverGround(*world, 4, block(u"stone"_s));
        const QcMobManager::PlayerView player = playerAt(*world, 0, 0);
        QcMobManager mobs(content, 9);
        const QList<Appearance> witches = witchesOf(run(mobs, *world, {player}, midnight, 6000));
        QVERIFY(witches.size() >= 5);
        for (const Appearance &witch : witches) {
            const double distance = (witch.feet - player.position).length();
            QVERIFY2(distance >= QcNaturalSpawner::monsterRadiusMin, qPrintable(QString::number(distance)));
        }
    }

    void hutsAreSpawnAreasTheSandGolemKeepsItsZone()
    {
        const QcStructureDefinition *hut = m_content->worldgen().structure(u"qtcraft:witch_hut"_s);
        QVERIFY(hut && hut->spawnArea);
        QCOMPARE(hut->spawnArea->monsters, QStringList{u"qtcraft:witch"_s});
        QVERIFY(hut->guardians.isEmpty());
        for (const QString &name : {u"qtcraft:desert_temple"_s, u"qtcraft:desert_temple_station"_s}) {
            const QcStructureDefinition *temple = m_content->worldgen().structure(name);
            QVERIFY(temple && !temple->spawnArea);
            QVERIFY(!temple->guardians.isEmpty());
            QCOMPARE(temple->guardians.front().mob, u"qtcraft:sand_golem"_s);
            QCOMPARE(temple->guardians.front().max, 1);
        }
        QCOMPARE(m_content->worldgen().structure(u"qtcraft:desert_temple"_s)->guardians.front().cooldownSeconds, 120.0);
        // The rule of a kind in an area keeps its light test and drops biome, time and weight.
        const QcMobDefinition::SpawnRule rule = QcSpawnAreas::ruleOf(m_content->mobs()[type(u"witch"_s)]);
        QVERIFY(rule.darkness);
        QCOMPARE(rule.maxLight, 0);
        QCOMPARE(rule.weight, 1.0);
        QVERIFY(rule.biomes.isEmpty() && rule.caves && !rule.water && !rule.sky);
    }

    void spawnAreaDataIsChecked()
    {
        const auto parse = [](const QString &area) {
            const QString text = u"{ \"name\": \"hut\", \"palette\": { \"#\": \"stone\" }, \"layers\": [ [\"##\"] ]%1 }"_s.arg(area);
            return QcContentParser::parseStructure(QJsonDocument::fromJson(text.toUtf8()).object(), u"qtcraft"_s);
        };
        const QcResult<QcStructureDefinition> plain = parse({});
        QVERIFY(plain && !plain->spawnArea);
        const QcResult<QcStructureDefinition> witches = parse(u", \"spawnArea\": { \"monsters\": [\"witch\"] }"_s);
        QVERIFY2(witches, qPrintable(witches ? QString() : witches.error()));
        QCOMPARE(witches->spawnArea->monsters, QStringList{u"qtcraft:witch"_s});
        for (const QString &wrong : {u", \"spawnArea\": { \"monsters\": [] }"_s, u", \"spawnArea\": { }"_s, u", \"spawnArea\": [\"witch\"]"_s,
                                     u", \"spawnArea\": { \"monsters\": [3] }"_s}) {
            const QcResult<QcStructureDefinition> refused = parse(wrong);
            QVERIFY2(!refused, qPrintable(wrong));
            QVERIFY2(refused.error().contains(u"spawnArea"_s), qPrintable(refused.error()));
        }

        // A kind that is no monster is reported and never comes.
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcStructureDefinition hut = *content->worldgen().structure(u"qtcraft:witch_hut"_s);
        hut.spawnArea->monsters = {u"qtcraft:cow"_s, u"qtcraft:witch"_s};
        content->worldgen().registerStructure(hut);
        const QStringList warnings = QcSpawnerContent::resolve(*content);
        QVERIFY2(warnings.contains(u"structure 'qtcraft:witch_hut': spawn area with 'qtcraft:cow', not a known monster, none come"_s),
                 qPrintable(warnings.join(u'\n')));
        QcSpawnAreas areas(content);
        auto world = makeWorld(content, 0, {witchArea({0, 0, 0}, {3, 0, 3})});
        areas.refresh(*world, {playerAt(*world, 0, 0)});
        QCOMPARE(areas.areas().size(), qsizetype{1});
        QCOMPARE(areas.areas().front().types, QList<int>{type(u"witch"_s)});
        QVERIFY(areas.at(3, 3) && !areas.at(4, 3));
    }

    void generatedHutsAreTheirFootprints()
    {
        // Every hut the terrain generator plans names its footprint as the witches' area (fixed
        // seeds and one drawn per run, decision E-846).
        int huts = 0;
        for (const qint64 seed : {qint64{513}, qint64{7}, qint64{20261017}, QcTestUtil::randomSeed()}) {
            const QcTerrainGenerator terrain(m_content, seed);
            const std::optional<int> kind = terrain.surfaceStructures().planner().kindIndex(u"qtcraft:witch_hut"_s);
            QVERIFY(kind);
            for (int rz = -4; rz <= 4; ++rz) {
                for (int rx = -4; rx <= 4; ++rx) {
                    const std::shared_ptr<const QcStructurePlacement> plan = terrain.surfaceStructures().planOf(*kind, rx, rz);
                    if (!plan) {
                        continue;
                    }
                    ++huts;
                    const QcStructureSites sites = terrain.structureSitesNear(plan->origin(), 16);
                    const auto area = std::ranges::find(sites.spawnAreas, QcSpawnArea::of(*plan)->min, &QcSpawnArea::min);
                    QVERIFY2(area != sites.spawnAreas.cend(), qPrintable(u"seed %1"_s.arg(seed)));
                    QCOMPARE(area->max.x - area->min.x + 1, plan->sizeX());
                    QCOMPARE(area->max.z - area->min.z + 1, plan->sizeZ());
                    QCOMPARE(area->min.x, std::min(plan->origin().x, plan->toWorld(QcBlockPos{plan->structure().sizeX() - 1, 0, 0}).x));
                    QCOMPARE(area->monsters, QStringList{u"qtcraft:witch"_s});
                    for (const QcGuardSite &site : sites.guards) {
                        QVERIFY(!area->containsColumn(site.min.x, site.min.z));
                    }
                }
            }
        }
        qInfo() << huts << "witch huts";
        QVERIFY(huts >= 2);
    }

    void yieldPerHour()
    {
        // The hut as built, and a farm with three more dark floors in its footprint; the player
        // waits 30 blocks west of it, every witch is taken away at once (decision E-871).
        const int hut = hourlyYield(0, 30);
        const int farm = hourlyYield(3, 30);
        qInfo().noquote() << u"witches per game hour: hut %1, farm with three more floors %2 (guardian zone before: at most 30)"_s.arg(hut).arg(farm);
        QVERIFY(hut > 0);
        // Every storey adds about what the hut gives (measured 18 and 92).
        QVERIFY2(farm >= 3 * hut, qPrintable(u"hut %1, farm %2"_s.arg(hut).arg(farm)));
    }

    void everyDarkFloorOfAnAreaColumnIsAPlace()
    {
        // An area of 8 × 8 columns, closed on every side, holds four dark rooms on top of each other.
        // An attempt that hits one of its columns tries every floor: each hit brings four witches in
        // that column at once (the roof under the noon sun gives none, the cap has room for 19).
        const QcSpawnArea area = witchArea({48, 0, -4}, {55, 0, 3});
        auto world = makeWorld(m_content, 4, {area});
        coverGround(*world, 4, block(u"glass"_s));
        const int ground = top(*world, 52, 0) + 2;
        const QcBlockPos min = area.min.offset(-1, 0, -1);
        const QcBlockPos max = area.max.offset(1, 0, 1);
        for (int y = ground; y <= ground + 12; ++y) {
            for (int x = min.x; x <= max.x; ++x) {
                for (int z = min.z; z <= max.z; ++z) {
                    const bool wall = x == min.x || x == max.x || z == min.z || z == max.z || (y - ground) % 3 == 0;
                    world->setBlock({x, y, z}, wall ? block(u"stone"_s) : qcAirBlockId);
                }
            }
        }
        QCOMPARE(world->skyLight({52, ground + 1, 0}), 0);

        QcMobManager mobs(m_content, 9);
        const QList<QcMobManager::PlayerView> players{playerAt(*world, 0, 0)};
        int hits = 0;
        for (int tick = 0; tick < 12000; ++tick) {
            Watch watch;
            mobs.tick(*world, players, noon, dt, [this] { return m_nextId++; });
            watch.look(mobs);
            mobs.removeAll();
            const QList<Appearance> witches = witchesOf(watch.appeared);
            if (witches.isEmpty()) {
                continue;
            }
            ++hits;
            QCOMPARE(witches.size(), qsizetype{4});
            QSet<int> floors;
            for (const Appearance &witch : witches) {
                QCOMPARE(QcPhysics::blockContaining(witch.feet).x, QcPhysics::blockContaining(witches.front().feet).x);
                QCOMPARE(QcPhysics::blockContaining(witch.feet).z, QcPhysics::blockContaining(witches.front().feet).z);
                floors.insert(QcPhysics::blockContaining(witch.feet).y);
            }
            QCOMPARE(floors, (QSet<int>{ground + 1, ground + 4, ground + 7, ground + 10}));
        }
        qInfo() << hits << "hits of the area";
        QVERIFY(hits >= 3);
    }

    void areasStayWhilePlanningCatchesUp()
    {
        const QcSpawnArea area = witchArea({48, 0, -4}, {55, 0, 3});
        auto terrain = std::make_unique<AreaTerrain>(m_content, QList<QcSpawnArea>{area});
        AreaTerrain *generator = terrain.get();
        QcServerWorld world(m_content, std::move(terrain));
        world.loadChunk({0, 0});
        QcSpawnAreas areas(m_content);
        const auto refresh = [&](const QList<QcMobManager::PlayerView> &players) {
            for (int call = 0; call < QcSpawnAreas::refreshTicks; ++call) {
                areas.refresh(world, players);
            }
        };

        // Two players near the same hut see it once.
        const QcMobManager::PlayerView near{7, {10.5, 70.0, 0.5}, true};
        refresh({near, {8, {20.5, 70.0, 5.5}, true}});
        QCOMPARE(areas.areas().size(), qsizetype{1});

        // Not planned: the player keeps the area it had; one far away gets none kept.
        generator->setPlanned(false);
        refresh({near});
        QCOMPARE(areas.areas().size(), qsizetype{1});
        refresh({{7, {near.position.x + 4 * QcSpawnAreas::reach, 70.0, 0.5}, true}});
        QVERIFY(areas.areas().isEmpty());
        generator->setPlanned(true);
        refresh({near});
        QCOMPARE(areas.areas().size(), qsizetype{1});
    }
};

QTEST_GUILESS_MAIN(TestWitchHutSpawns)
#include "test_witch_hut_spawns.moc"
