#include "QcTestUtil.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/towers/QcCoastalTowerGenerator.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

using namespace Qt::StringLiterals;

// Coastal towers of the terrain generator (decision E-605): plans are a pure function of seed and
// region, towers stand on low land with the open sea near them, the chunks of a tower come out the
// same in any order with the structure and its foundation in place, the top room's chest gets loot
// from its table, the trap is a pressure plate on an explosive in the floor with no other power
// source near the explosives, the showcase has a station far enough from its neighbours, and no
// tower stands near a surface structure.
class TestCoastalTowers : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 512;
    static constexpr int regions = 3;
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;
    using Plans = QList<std::shared_ptr<const QcCoastalTowerPlan>>;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcTerrainGenerator> m_terrain;
    Plans m_plans;

    static Plans plans(const QcTerrainGenerator &terrain)
    {
        Plans found;
        for (int rz = -regions; rz <= regions; ++rz) {
            for (int rx = -regions; rx <= regions; ++rx) {
                if (std::shared_ptr<const QcCoastalTowerPlan> plan = terrain.coastalTowers().planOf(rx, rz)) {
                    found.append(std::move(plan));
                }
            }
        }
        return found;
    }

    static QList<QcChunkPos> chunksOf(const QcCoastalTowerPlan &plan)
    {
        const QcCoastalTowerPlan::Footprint footprint = plan.footprint().grown(1);
        QList<QcChunkPos> chunks;
        for (int cx = QcBlockPos{footprint.minX, 0, 0}.chunk().x; cx <= QcBlockPos{footprint.maxX, 0, 0}.chunk().x; ++cx) {
            for (int cz = QcBlockPos{0, 0, footprint.minZ}.chunk().z; cz <= QcBlockPos{0, 0, footprint.maxZ}.chunk().z; ++cz) {
                chunks.append({cx, cz});
            }
        }
        return chunks;
    }

    static Chunks generate(const QcWorldGenerator &generator, const QList<QcChunkPos> &positions)
    {
        Chunks chunks;
        for (const QcChunkPos &pos : positions) {
            chunks.insert(pos, generator.generate(pos));
        }
        return chunks;
    }

    static QcBlockId at(const Chunks &chunks, QcBlockPos pos)
    {
        const std::shared_ptr<QcChunk> chunk = chunks.value(pos.chunk());
        return chunk && pos.isInsideWorldHeight() ? chunk->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
    }

    static bool sameBlocks(const QcChunk &a, const QcChunk &b)
    {
        for (int y = 0; y < QcWorld::worldHeight; ++y) {
            for (int i = 0; i < QcWorld::chunkSize * QcWorld::chunkSize; ++i) {
                if (a.block(i % QcWorld::chunkSize, y, i / QcWorld::chunkSize) != b.block(i % QcWorld::chunkSize, y, i / QcWorld::chunkSize)) {
                    return false;
                }
            }
        }
        return true;
    }

    const QcBlockDefinition &block(QcBlockId id) const { return m_content->block(id); }
    QString nameOf(QcBlockId id) const { return block(id).name; }

    // Blocks that give signal power on their own or carry it (dust, levers, buttons, plates,
    // torches, repeaters, comparators, observers).
    bool givesPower(QcBlockId id) const
    {
        switch (block(id).signal.kind) {
        case QcSignalKind::Dust:
        case QcSignalKind::Lever:
        case QcSignalKind::Button:
        case QcSignalKind::Plate:
        case QcSignalKind::Torch:
        case QcSignalKind::Repeater:
        case QcSignalKind::Comparator:
        case QcSignalKind::Observer:
            return true;
        default:
            return false;
        }
    }

    // The cells of the block `name` in the box from `box.first` to `box.second`.
    QList<QcBlockPos> cellsOf(const Chunks &chunks, const std::pair<QcBlockPos, QcBlockPos> &box, const QString &name) const
    {
        QList<QcBlockPos> cells;
        for (int y = box.first.y; y <= box.second.y; ++y) {
            for (int z = box.first.z; z <= box.second.z; ++z) {
                for (int x = box.first.x; x <= box.second.x; ++x) {
                    if (nameOf(at(chunks, {x, y, z})) == name) {
                        cells.append({x, y, z});
                    }
                }
            }
        }
        return cells;
    }

    // The trap of a built tower in `chunks`: its plate stands on an explosive, there are 3 to 5
    // explosives, and no block within two cells (taxicab) of an explosive gives power but the plate.
    void checkTrap(const Chunks &chunks, QcBlockPos low, QcBlockPos high)
    {
        const QList<QcBlockPos> plates = cellsOf(chunks, {low, high}, u"qtcraft:stone_pressure_plate"_s);
        const QList<QcBlockPos> explosives = cellsOf(chunks, {low, high}, u"qtcraft:explosive"_s);
        QCOMPARE(plates.size(), 1);
        const QcBlockPos plate = plates.constFirst();
        QCOMPARE(nameOf(at(chunks, plate.offset(0, -1, 0))), u"qtcraft:explosive"_s);
        QVERIFY2(explosives.size() >= 3 && explosives.size() <= 5, qPrintable(QString::number(explosives.size())));

        for (const QcBlockPos &explosive : std::as_const(explosives)) {
            QVERIFY(explosive.y == plate.y - 1);
            const QString power = powerNear(chunks, explosive, plate);
            QVERIFY2(power.isEmpty(), qPrintable(power));
        }
    }

    // A block that gives power within two cells (taxicab) of `explosive`, other than `plate`; empty
    // without one.
    QString powerNear(const Chunks &chunks, QcBlockPos explosive, QcBlockPos plate) const
    {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const QcBlockPos near = explosive.offset(dx, dy, dz);
                    if (std::abs(dx) + std::abs(dy) + std::abs(dz) <= 2 && near != plate && givesPower(at(chunks, near))) {
                        return nameOf(at(chunks, near));
                    }
                }
            }
        }
        return {};
    }

    // The footprints of every surface structure planned within `reach` blocks of the origin, counted
    // by kind in `copies`.
    static QList<QcCoastalTowerPlan::Footprint> surfaceFootprints(const QcTerrainGenerator &terrain, int reach, QHash<QString, qsizetype> &copies)
    {
        QList<QcCoastalTowerPlan::Footprint> structures;
        const QcSurfaceStructures &surface = terrain.surfaceStructures();
        for (qsizetype kind = 0; kind < surface.planner().kinds().size(); ++kind) {
            const QcSurfacePlanner::Kind &rule = surface.planner().kinds()[kind];
            const int regions = reach / rule.rule.spacing + 1;
            for (int rz = -regions; rz <= regions; ++rz) {
                for (int rx = -regions; rx <= regions; ++rx) {
                    if (const std::shared_ptr<const QcStructurePlacement> copy = surface.planOf(static_cast<int>(kind), rx, rz)) {
                        structures.append(QcCoastalTowerPlan::footprintOf(*copy));
                        ++copies[rule.name];
                    }
                }
            }
        }
        return structures;
    }

private slots:
    void initTestCase()
    {
        m_terrain = std::make_unique<QcTerrainGenerator>(m_content, seed);
        QVERIFY(m_terrain->coastalTowers().planner().enabled());
        m_plans = plans(*m_terrain);
        qInfo().noquote() << u"%1 coastal towers in %2 regions"_s.arg(m_plans.size()).arg((2 * regions + 1) * (2 * regions + 1));
        QVERIFY(m_plans.size() >= 4);
    }

    // Same seed, same towers: a fresh generator plans every region alike, each tower in its region.
    void sameSeedSameTowers()
    {
        const QcTerrainGenerator again(m_content, seed);
        const Plans second = plans(again);
        QCOMPARE(second.size(), m_plans.size());
        const int size = m_content->worldgen().terrainSettings().coastalTowers.regionSize;
        for (qsizetype i = 0; i < m_plans.size(); ++i) {
            const QcCoastalTowerPlan &a = *m_plans[i];
            const QcCoastalTowerPlan &b = *second[i];
            QCOMPARE(a.placement.origin(), b.placement.origin());
            QCOMPARE(a.placement.turns(), b.placement.turns());
            QCOMPARE(a.placement.structure().name(), b.placement.structure().name());
            QCOMPARE(a.ocean, b.ocean);
            const QcCoastalTowerPlan::Footprint footprint = a.footprint();
            QVERIFY(footprint.minX >= a.regionX * size && footprint.maxX < (a.regionX + 1) * size);
            QVERIFY(footprint.minZ >= a.regionZ * size && footprint.maxZ < (a.regionZ + 1) * size);
        }

        // Another seed, other towers.
        const QcTerrainGenerator other(m_content, seed + 1);
        const Plans third = plans(other);
        QVERIFY(third.size() != m_plans.size() || third.constFirst()->placement.origin() != m_plans.constFirst()->placement.origin());
    }

    // Every tower stands on land at a tower's height, with the open sea within the distance.
    void towersStandAtCoasts()
    {
        const QcCoastalTowerPlanner &planner = m_terrain->coastalTowers().planner();
        const QcCoastalTowerSettings &settings = planner.settings();
        const int sea = m_terrain->seaLevel();
        for (const std::shared_ptr<const QcCoastalTowerPlan> &plan : std::as_const(m_plans)) {
            const QcCoastalTowerPlan::Footprint footprint = plan->footprint();
            for (int z = footprint.minZ; z <= footprint.maxZ; ++z) {
                for (int x = footprint.minX; x <= footprint.maxX; ++x) {
                    const int top = m_terrain->column(x, z).height - 1;
                    QVERIFY(top >= sea && top <= sea + settings.maxRise);
                    QVERIFY(top <= plan->placement.origin().y);
                }
            }

            QVERIFY(planner.isOcean(plan->ocean.x, plan->ocean.z));
            QVERIFY(footprint.grown(settings.coastDistance).contains(plan->ocean.x, plan->ocean.z));
            const QcTerrainGenerator::Column ocean = m_terrain->column(plan->ocean.x, plan->ocean.z);
            QVERIFY(ocean.landFactor < 0.5);
            QVERIFY(ocean.height - 1 <= sea - 1 - settings.oceanDepth);
        }
    }

    // The chunks of a tower come out the same in any order, with the structure written and the
    // foundation down to the ground; the door opens onto ground one can step from.
    void towersAreBuiltInAnyOrder()
    {
        const QcCoastalTowerSettings &settings = m_content->worldgen().terrainSettings().coastalTowers;
        for (qsizetype i = 0; i < std::min<qsizetype>(3, m_plans.size()); ++i) {
            const QcCoastalTowerPlan &plan = *m_plans[i];
            QList<QcChunkPos> positions = chunksOf(plan);
            const Chunks forward = generate(*m_terrain, positions);
            std::ranges::reverse(positions);
            const QcTerrainGenerator fresh(m_content, seed);
            const Chunks backward = generate(fresh, positions);
            for (const QcChunkPos &pos : std::as_const(positions)) {
                QVERIFY(sameBlocks(*forward.value(pos), *backward.value(pos)));
            }

            const QcStructurePlacement &placement = plan.placement;
            const QcStructure &structure = placement.structure();
            for (int y = 0; y < structure.sizeY(); ++y) {
                for (int z = 0; z < structure.sizeZ(); ++z) {
                    for (int x = 0; x < structure.sizeX(); ++x) {
                        const QcBlockPos local{x, y, z};
                        const std::optional<QcBlockId> expected = placement.worldBlock(local);
                        const QcBlockId found = at(forward, placement.toWorld(local));
                        QVERIFY2(!expected || *expected == found, qPrintable(nameOf(found)));
                    }
                }
            }

            const QcCoastalTowerPlan::Footprint footprint = plan.footprint();
            const int floor = placement.origin().y;
            for (int z = footprint.minZ; z <= footprint.maxZ; ++z) {
                for (int x = footprint.minX; x <= footprint.maxX; ++x) {
                    int y = floor - 1;
                    while (nameOf(at(forward, {x, y, z})) == settings.foundation) {
                        --y;
                    }
                    const QcBlockDefinition &ground = block(at(forward, {x, y, z}));
                    QVERIFY2(ground.solid && ground.opaque && !ground.liquid, qPrintable(ground.name));
                }
            }
            QVERIFY(std::abs(plan.doorstep.y - floor) <= 1);
            QVERIFY(block(at(forward, plan.doorstep)).solid);
            QVERIFY(!block(at(forward, plan.doorstep.offset(0, 1, 0))).solid);
        }
    }

    // The top room's chest is furnished from the loot table, which gives gunpowder, explosives and
    // treasure; the trap is armed.
    void theTopRoomHasLootAndATrap()
    {
        const QcCoastalTowerPlan &plan = *m_plans.constFirst();
        const QcStructurePlacement &placement = plan.placement;
        QCOMPARE(placement.structure().loot.size(), 1);
        const QcBlockPos chest = placement.toWorld(placement.structure().loot.constFirst().at);
        const QcChunkFurnishing furnishing = m_terrain->furnishingIn(chest.chunk());
        QVERIFY(std::ranges::any_of(furnishing.chests, [&](const QcChunkFurnishing::Chest &placed) {
            return placed.at == chest && placed.lootTable == u"qtcraft:coastal_tower"_s;
        }));

        const Chunks chunks = generate(*m_terrain, chunksOf(plan));
        QCOMPARE(nameOf(at(chunks, chest)), u"qtcraft:chest"_s);
        const QcCoastalTowerPlan::Footprint footprint = plan.footprint();
        const int floor = placement.origin().y;
        checkTrap(chunks, {footprint.minX, floor, footprint.minZ}, {footprint.maxX, floor + placement.structure().sizeY() - 1, footprint.maxZ});

        const QcLootTableDefinition *table = m_content->lootTables().find(u"qtcraft:coastal_tower"_s);
        QVERIFY(table);
        const auto items = [this](const QString &name) -> std::optional<QcLootTables::ItemInfo> {
            const std::optional<QcItemId> item = m_content->itemId(name);
            return item ? std::optional(QcLootTables::ItemInfo{*item, m_content->item(*item).maxStack}) : std::nullopt;
        };
        QSet<QString> seen;
        for (quint64 draw = 0; draw < 200; ++draw) {
            const QList<QcItemStack> stacks = QcLootTables::draw(*table, draw * 7919, items);
            for (const QcItemStack &stack : stacks) {
                seen.insert(m_content->item(stack.item).name);
            }
        }
        for (const char *name : {"qtcraft:gunpowder", "qtcraft:explosive", "qtcraft:iron_ingot", "qtcraft:gold_ingot", "qtcraft:emerald"}) {
            QVERIFY2(seen.contains(QString::fromLatin1(name)), name);
        }
    }

    // The showcase's station: the tower on a beach by the sea, its chest furnished, the trap armed,
    // and every other structure of the showcase out of reach of the explosions (power 4 breaks
    // blocks up to about seven blocks away in open air).
    void theShowcaseStationShowsATower()
    {
        const QcShowcaseGenerator generator(m_content);
        const QList<QcStructurePlacement> &placements = generator.plan().placements;
        const auto station = std::ranges::find_if(placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name() == u"qtcraft:coastal_tower_station"_s;
        });
        QVERIFY(station != placements.end());
        const QcStructure &structure = station->structure();
        QCOMPARE(structure.loot.size(), 1);
        const QcBlockPos chest = station->toWorld(structure.loot.constFirst().at);
        QVERIFY(std::ranges::any_of(generator.furnishingIn(chest.chunk()).chests, [&](const QcChunkFurnishing::Chest &placed) {
            return placed.at == chest && placed.lootTable == u"qtcraft:coastal_tower"_s;
        }));

        QList<QcChunkPos> positions;
        for (int x = 0; x < station->sizeX(); ++x) {
            for (int z = 0; z < station->sizeZ(); ++z) {
                const QcChunkPos pos = station->toWorld(QcBlockPos{x, 0, z}).chunk();
                if (!positions.contains(pos)) {
                    positions.append(pos);
                }
            }
        }
        const Chunks chunks = generate(generator, positions);
        const QcBlockPos low = station->toWorld(QcBlockPos{0, 0, 0});
        const QcBlockPos high = station->toWorld(QcBlockPos{station->sizeX() - 1, structure.sizeY() - 1, station->sizeZ() - 1});
        QCOMPARE(nameOf(at(chunks, chest)), u"qtcraft:chest"_s);
        checkTrap(chunks, low, high);
        QCOMPARE(nameOf(at(chunks, station->toWorld(QcBlockPos{13, 0, 7}))), u"qtcraft:water"_s);

        // The plate's explosive lies at least nine blocks from every other structure of the showcase
        // (the other explosives lie next to it), measured across the ground: a blast reaches as far
        // sideways at any height of a neighbour.
        const QcBlockPos trap = station->toWorld(QcBlockPos{7, 12, 6});
        QCOMPARE(nameOf(at(chunks, trap)), u"qtcraft:explosive"_s);
        for (const QcStructurePlacement &other : placements) {
            if (&other == &*station) {
                continue;
            }
            const QcBlockPos a = other.toWorld(QcBlockPos{0, 0, 0});
            const QcBlockPos b = other.toWorld(QcBlockPos{other.sizeX() - 1, other.structure().sizeY() - 1, other.sizeZ() - 1});
            const auto gap = [](int value, int x0, int x1) { return value < std::min(x0, x1) ? std::min(x0, x1) - value : std::max(0, value - std::max(x0, x1)); };
            const int dx = gap(trap.x, a.x, b.x);
            const int dz = gap(trap.z, a.z, b.z);
            QVERIFY2(dx * dx + dz * dz >= 81, qPrintable(other.structure().name()));
        }
    }

    // Towers and surface structures (desert temples, ruins, wells, oases, jungle temples, witch huts)
    // never stand within the towers' clearance of each other (decision E-605), over a wide area of
    // several seeds: every tower against every copy of every kind planned around it. Towers keep
    // `minDistance` (300 blocks) from each other and come at about one per 10 km² of the world
    // (decision E-857); fixed seeds plus one random seed per run (decision E-846).
    void towersKeepClearOfSurfaceStructures()
    {
        constexpr int towerRegions = 5;
        const QcCoastalTowerSettings &settings = m_content->worldgen().terrainSettings().coastalTowers;
        const int size = settings.regionSize;
        const int clearance = settings.clearance;
        const int reach = (towerRegions + 1) * size;
        qsizetype towers = 0;
        qsizetype near = 0;
        double nearest = 1.0e9;
        QHash<QString, qsizetype> copies;
        for (const qint64 each : {seed, qint64{51}, qint64{7}, QcTestUtil::randomSeed()}) {
            const QcTerrainGenerator terrain(m_content, each);
            const QList<QcCoastalTowerPlan::Footprint> structures = surfaceFootprints(terrain, reach, copies);
            QList<std::pair<int, int>> centres;
            for (int rz = -towerRegions; rz <= towerRegions; ++rz) {
                for (int rx = -towerRegions; rx <= towerRegions; ++rx) {
                    const std::shared_ptr<const QcCoastalTowerPlan> tower = terrain.coastalTowers().planOf(rx, rz);
                    if (!tower) {
                        continue;
                    }
                    ++towers;
                    const QcCoastalTowerPlan::Footprint footprint = tower->footprint();
                    const std::pair centre{footprint.minX + footprint.maxX, footprint.minZ + footprint.maxZ};
                    for (const auto &[x, z] : std::as_const(centres)) {
                        nearest = std::min(nearest, std::hypot(x - centre.first, z - centre.second) / 2.0);
                    }
                    centres.append(centre);
                    const QcCoastalTowerPlan::Footprint around = tower->footprint().grown(clearance);
                    for (const QcCoastalTowerPlan::Footprint &structure : std::as_const(structures)) {
                        QVERIFY2(!around.touches(structure), qPrintable(u"seed %1 tower at %2 %3"_s.arg(each).arg(around.minX).arg(around.minZ)));
                        near += around.grown(64).touches(structure) ? 1 : 0;
                    }
                }
            }
        }
        const double area = 4.0 * std::pow((2 * towerRegions + 1) * size / 1000.0, 2.0);
        qInfo().noquote() << u"%1 towers (%2 per km²), nearest two %3 blocks apart, %4 within 64 blocks of a surface structure"_s.arg(towers)
                                 .arg(static_cast<double>(towers) / area, 0, 'f', 3)
                                 .arg(nearest, 0, 'f', 0)
                                 .arg(near)
                          << copies;
        QVERIFY2(nearest >= settings.minDistance, qPrintable(QString::number(nearest)));
        QVERIFY(towers >= 40);
        // Some towers stand close to a surface structure, so the clearance is put to the test.
        QVERIFY(near > 0);
        for (const QString &kind : {u"qtcraft:desert_temple"_s, u"qtcraft:jungle_temple"_s, u"qtcraft:witch_hut"_s, u"qtcraft:oasis"_s,
                                   u"qtcraft:well"_s, u"qtcraft:desert_well"_s, u"qtcraft:ruined_portal"_s}) {
            QVERIFY2(copies.value(kind) > 0, qPrintable(kind));
        }
    }

    // Debug-relative cost of the coastal towers (stage 51.2, docs/Performance-Verlauf.md): chunks
    // per second of thread time with and without them, including the planning of every region met,
    // on two areas interleaved with fresh generators. Printed, not checked.
    void measureGenerationCost()
    {
        auto without = std::make_shared<QcContentRegistry>(*m_content);
        QcTerrainSettings settings = without->worldgen().terrainSettings();
        settings.coastalTowers.enabled = false;
        without->worldgen().setTerrainSettings(settings);
        const auto seconds = [](const QcTerrainGenerator &terrain, const QList<QcChunkPos> &chunks) {
            timespec start{};
            timespec end{};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
            for (const QcChunkPos &pos : chunks) {
                terrain.generate(pos);
            }
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
            return static_cast<double>(end.tv_sec - start.tv_sec) + static_cast<double>(end.tv_nsec - start.tv_nsec) / 1e9;
        };
        const QcTerrainGenerator with(m_content, seed);
        const QcTerrainGenerator plain(without, seed);
        double withTime = 0.0;
        double plainTime = 0.0;
        qsizetype count = 0;
        for (const auto &[x, z] : {std::pair{0, 0}, {-40, -40}}) {
            QList<QcChunkPos> chunks;
            for (int cz = 0; cz < 6; ++cz) {
                for (int cx = 0; cx < 6; ++cx) {
                    chunks.append({cx + x, cz + z});
                }
            }
            plainTime += seconds(plain, chunks);
            withTime += seconds(with, chunks);
            count += chunks.size();
        }
        qInfo().noquote() << u"chunks/s without coastal towers %1, with %2"_s.arg(static_cast<double>(count) / plainTime, 0, 'f', 1)
            .arg(static_cast<double>(count) / withTime, 0, 'f', 1);
    }
};

QTEST_GUILESS_MAIN(TestCoastalTowers)
#include "test_coastal_towers.moc"
