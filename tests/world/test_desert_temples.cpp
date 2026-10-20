#include "common/core/QcIntMath.h"
#include "QcTestUtil.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QTest>

#include <algorithm>
#include <ctime>

using namespace Qt::StringLiterals;

// Desert temples (decisions E-590..E-592): structures the terrain generator places by itself from
// their `surface` rule, planned from the seed and the terrain noise alone (the same plans with a fresh
// generator, chunks the same in any order), only in the desert, never near a village; their chests
// get loot and their cellar is a guardian zone. With a chance a desert village has the temple as its
// centre instead of the well, cellar and loot included.
class TestDesertTemples : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 51;
    static constexpr int regions = 6;
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcTerrainGenerator> m_terrain;
    int m_kind = -1;
    QList<std::shared_ptr<const QcStructurePlacement>> m_plans;

    QList<std::shared_ptr<const QcStructurePlacement>> plans(const QcTerrainGenerator &terrain) const
    {
        QList<std::shared_ptr<const QcStructurePlacement>> found;
        for (int rz = -regions; rz <= regions; ++rz) {
            for (int rx = -regions; rx <= regions; ++rx) {
                if (std::shared_ptr<const QcStructurePlacement> plan = terrain.surfaceStructures().planOf(m_kind, rx, rz)) {
                    found.append(std::move(plan));
                }
            }
        }
        return found;
    }

    static QList<QcChunkPos> chunksOf(const QcStructurePlacement &plan)
    {
        QList<QcChunkPos> chunks;
        const QcChunkPos first = plan.origin().chunk();
        const QcChunkPos last = plan.origin().offset(plan.sizeX() - 1, 0, plan.sizeZ() - 1).chunk();
        for (int cx = first.x; cx <= last.x; ++cx) {
            for (int cz = first.z; cz <= last.z; ++cz) {
                chunks.append({cx, cz});
            }
        }
        return chunks;
    }

    static Chunks generate(const QcWorldGenerator &terrain, const QList<QcChunkPos> &positions)
    {
        Chunks chunks;
        for (const QcChunkPos &pos : positions) {
            chunks.insert(pos, terrain.generate(pos));
        }
        return chunks;
    }

    static QcBlockId at(const Chunks &chunks, QcBlockPos pos)
    {
        const std::shared_ptr<QcChunk> chunk = chunks.value(pos.chunk());
        return chunk ? chunk->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
    }

    QString nameAt(const Chunks &chunks, QcBlockPos pos) const { return m_content->block(at(chunks, pos)).name; }

    // Every cell the placement writes holds its block in the chunks.
    static void verifyWritten(const QcStructurePlacement &plan, const Chunks &chunks)
    {
        const QcStructure &structure = plan.structure();
        for (int y = 0; y < structure.sizeY(); ++y) {
            for (int z = 0; z < structure.sizeZ(); ++z) {
                for (int x = 0; x < structure.sizeX(); ++x) {
                    const std::optional<QcBlockId> block = plan.worldBlock({x, y, z});
                    if (block) {
                        QCOMPARE(at(chunks, plan.toWorld(QcBlockPos{x, y, z})), *block);
                    }
                }
            }
        }
    }

    // The content with the desert style's rare centre always taken (or the temple off the surface).
    std::shared_ptr<QcContentRegistry> contentWith(double rareCentreChance, bool surface) const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcVillageStyleDefinition desert = *std::ranges::find(content->villageCatalog().styles(), u"qtcraft:desert"_s, &QcVillageStyleDefinition::name);
        desert.rareCentreChance = rareCentreChance;
        content->villageCatalog().registerStyle(desert);
        if (!surface) {
            QcStructureDefinition temple = *content->worldgen().structure(u"qtcraft:desert_temple"_s);
            temple.surface.reset();
            content->worldgen().registerStructure(temple);
        }
        return content;
    }

private slots:
    void initTestCase()
    {
        m_terrain = std::make_unique<QcTerrainGenerator>(m_content, seed);
        const std::optional<int> kind = m_terrain->surfaceStructures().planner().kindIndex(u"qtcraft:desert_temple"_s);
        QVERIFY(kind);
        m_kind = *kind;
        m_plans = plans(*m_terrain);
        qInfo().noquote() << u"%1 desert temples in %2 regions"_s.arg(m_plans.size()).arg((2 * regions + 1) * (2 * regions + 1));
        QVERIFY(m_plans.size() >= 3);
    }

    void plansAreDeterministicAndInTheDesert()
    {
        // A fresh generator plans the same, in the other order of the regions.
        const QcTerrainGenerator fresh(m_content, seed);
        for (qsizetype i = m_plans.size() - 1; i >= 0; --i) {
            const QcStructurePlacement &plan = *m_plans[i];
            const int spacing = 320;
            const int rx = qcFloorDiv(plan.origin().x, spacing);
            const int rz = qcFloorDiv(plan.origin().z, spacing);
            const std::shared_ptr<const QcStructurePlacement> again = fresh.surfaceStructures().planOf(m_kind, rx, rz);
            QVERIFY(again);
            QCOMPARE(again->origin(), plan.origin());
            QCOMPARE(again->turns(), plan.turns());
            // Wholly in its region, in the desert at its corners and middle.
            QCOMPARE(qcFloorDiv(plan.origin().x + plan.sizeX() - 1, spacing), rx);
            QCOMPARE(qcFloorDiv(plan.origin().z + plan.sizeZ() - 1, spacing), rz);
            // Its floor on the lowest ground: no corner or edge middle hangs above it.
            const int floor = plan.origin().y + plan.structure().groundLayer;
            for (const auto &[dx, dz] : {std::pair{0, 0}, {plan.sizeX() - 1, plan.sizeZ() - 1}, {plan.sizeX() / 2, plan.sizeZ() / 2}, {plan.sizeX() / 2, 0},
                                         {plan.sizeX() / 2, plan.sizeZ() - 1}, {0, plan.sizeZ() / 2}, {plan.sizeX() - 1, plan.sizeZ() / 2}}) {
                QCOMPARE(m_terrain->biomeAt(plan.origin().x + dx, plan.origin().z + dz), u"qtcraft:desert"_s);
                QVERIFY(m_terrain->column(plan.origin().x + dx, plan.origin().z + dz).height - 1 >= floor);
            }
        }
        // Another seed plans elsewhere.
        const QcTerrainGenerator other(m_content, seed + 1);
        const QList<std::shared_ptr<const QcStructurePlacement>> otherPlans = plans(other);
        QVERIFY(otherPlans.isEmpty() || otherPlans.front()->origin() != m_plans.front()->origin());
    }

    void templesKeepClearOfVillages()
    {
        for (const std::shared_ptr<const QcStructurePlacement> &plan : std::as_const(m_plans)) {
            const QcSurfacePlanner::Rect rect = QcSurfacePlanner::footprintOf(*plan);
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const QcBlockPos probe = plan->origin().offset(dx * 384, 0, dz * 384);
                    const std::shared_ptr<const QcVillagePlan> village = m_terrain->villages().planFor(probe.chunk());
                    QVERIFY(!village || !rect.intersects({village->minX, village->minZ, village->maxX, village->maxZ}, 16));
                }
            }
        }
    }

    void chunksAreTheSameInAnyOrder()
    {
        const QcStructurePlacement &plan = *m_plans.front();
        QList<QcChunkPos> positions = chunksOf(plan);
        const QcTerrainGenerator forward(m_content, seed);
        const Chunks first = generate(forward, positions);
        std::ranges::reverse(positions);
        const QcTerrainGenerator backward(m_content, seed);
        const Chunks second = generate(backward, positions);
        for (const QcChunkPos &pos : std::as_const(positions)) {
            for (int y = 0; y < QcWorld::worldHeight; ++y) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        QCOMPARE(second[pos]->block(x, y, z), first[pos]->block(x, y, z));
                    }
                }
            }
        }
        verifyWritten(plan, first);

        // Cleared above the roof, the cellar open below the ground, the floor at the ground's top.
        const QcStructure &structure = plan.structure();
        QCOMPARE(structure.groundLayer, 5);
        QCOMPARE(at(first, plan.toWorld(QcBlockPos{7, structure.sizeY() + 1, 7})), qcAirBlockId);
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{7, 1, 7})), u"qtcraft:air"_s);
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{7, 3, 2})), u"qtcraft:ladder"_s);
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{4, 1, 3})), u"qtcraft:chest"_s);
    }

    void chestsAndCellarAreFurnished()
    {
        const QcStructurePlacement &plan = *m_plans.front();
        int chests = 0;
        const QList<QcChunkPos> planChunks = chunksOf(plan);
        for (const QcChunkPos &pos : planChunks) {
            const QcChunkFurnishing furnishing = m_terrain->furnishingIn(pos);
            for (const QcChunkFurnishing::Chest &chest : furnishing.chests) {
                QCOMPARE(chest.at.chunk(), pos);
                if (chest.lootTable == u"qtcraft:desert_temple_chest"_s) {
                    ++chests;
                }
            }
        }
        QCOMPARE(chests, 4);

        // The cellar is a guardian zone of the sand golem, on the cellar's floor.
        const QcBlockPos middle = plan.toWorld(QcBlockPos{7, 1, 7});
        const QList<QcGuardSite> sites = m_terrain->guardSitesNear(middle, 64);
        const auto site = std::ranges::find_if(sites, [&](const QcGuardSite &s) { return s.contains(middle); });
        QVERIFY(site != sites.cend());
        QCOMPARE(site->rule.mob, u"qtcraft:sand_golem"_s);
        QCOMPARE(site->min.y, middle.y);
        QCOMPARE(site->max.x - site->min.x, 6);
        QVERIFY(m_content->lootTables().find(u"qtcraft:desert_temple_chest"_s));
    }

    void someDesertVillagesAreBuiltAroundATemple()
    {
        // With the chance at 1 every desert village that has room takes the temple as its centre.
        const auto content = contentWith(1.0, true);
        const QcTerrainGenerator terrain(content, seed);
        std::shared_ptr<const QcVillagePlan> found;
        for (int rz = -4; rz <= 4 && !found; ++rz) {
            for (int rx = -4; rx <= 4 && !found; ++rx) {
                const std::shared_ptr<const QcVillagePlan> plan = terrain.villages().regionPlan(rx, rz);
                if (plan && plan->style == u"qtcraft:desert"_s && plan->pieces.front().structure().name() == u"qtcraft:desert_temple"_s) {
                    found = plan;
                }
            }
        }
        QVERIFY(found);
        const QcStructurePlacement &temple = found->pieces.front();
        // The roads keep clear of it and its entrance faces the main road; its floor lies at the
        // ground and the village's meeting point in its hall.
        QCOMPARE(found->centre.y, temple.origin().y + temple.structure().groundLayer + 1);

        // Its chunks hold the cellar, its chests get loot and its cellar is a guardian zone.
        const Chunks chunks = generate(terrain, chunksOf(temple));
        verifyWritten(temple, chunks);
        int chests = 0;
        const QList<QcChunkPos> templeChunks = chunksOf(temple);
        for (const QcChunkPos &pos : templeChunks) {
            chests += int(std::ranges::count(terrain.furnishingIn(pos).chests, u"qtcraft:desert_temple_chest"_s, &QcChunkFurnishing::Chest::lootTable));
        }
        QCOMPARE(chests, 4);
        const QcBlockPos cellar = temple.toWorld(QcBlockPos{7, 1, 7});
        const QList<QcGuardSite> sites = terrain.guardSitesNear(cellar, 32);
        QVERIFY(std::ranges::any_of(sites, [&](const QcGuardSite &site) { return site.contains(cellar); }));
        // /locate finds it as well (QcWorldGenerator::nearestStructure).
        const std::optional<QcStructurePlacement> located = terrain.nearestStructure(u"qtcraft:desert_temple"_s, found->centre, 32);
        QVERIFY(located);
        QCOMPARE(located->origin(), temple.origin());

        // Without the chance the same region has its well.
        const QcTerrainGenerator plain(contentWith(0.0, true), seed);
        const int spacing = terrain.villages().spacing();
        const std::shared_ptr<const QcVillagePlan> well =
            plain.villages().regionPlan(qcFloorDiv(found->centre.x, spacing), qcFloorDiv(found->centre.z, spacing));
        QVERIFY(well);
        QCOMPARE(well->pieces.front().structure().name(), u"qtcraft:village_desert_well"_s);
    }

    void measureGenerationCost()
    {
        const auto without = contentWith(0.0, false);
        const auto seconds = [](const QcTerrainGenerator &terrain, const QList<QcChunkPos> &chunks) {
            timespec start{};
            timespec end{};
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
            for (const QcChunkPos &pos : chunks) {
                terrain.generate(pos);
            }
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
            return double(end.tv_sec - start.tv_sec) + double(end.tv_nsec - start.tv_nsec) / 1e9;
        };

        // The chunks of the temples, fresh generators interleaved (plans made afresh).
        QList<QcChunkPos> chunks;
        for (qsizetype i = 0; i < std::min<qsizetype>(3, m_plans.size()); ++i) {
            chunks.append(chunksOf(*m_plans[i]));
        }
        double withTime = 0.0;
        double plainTime = 0.0;
        for (int round = 0; round < 2; ++round) {
            const QcTerrainGenerator plain(without, seed);
            const QcTerrainGenerator with(m_content, seed);
            plainTime += seconds(plain, chunks);
            withTime += seconds(with, chunks);
        }
        const double count = 2.0 * double(chunks.size());
        qInfo().noquote() << u"chunks/s of %1 temple chunks without temples %2, with %3"_s.arg(chunks.size())
                                 .arg(count / plainTime, 0, 'f', 1)
                                 .arg(count / withTime, 0, 'f', 1);

        // An area of the desert world with its temples planned afresh.
        QList<QcChunkPos> area;
        const QcChunkPos corner = m_plans.front()->origin().chunk();
        for (int cz = -6; cz < 6; ++cz) {
            for (int cx = -6; cx < 6; ++cx) {
                area.append({corner.x + cx, corner.z + cz});
            }
        }
        const QcTerrainGenerator plain(without, seed);
        const QcTerrainGenerator with(m_content, seed);
        const double a = seconds(plain, area);
        const double b = seconds(with, area);
        qInfo().noquote() << u"chunks/s of %1 chunks around a temple without temples %2, with %3"_s.arg(area.size())
                                 .arg(double(area.size()) / a, 0, 'f', 1)
                                 .arg(double(area.size()) / b, 0, 'f', 1);
    }
};

QTEST_GUILESS_MAIN(TestDesertTemples)
#include "test_desert_temples.moc"
