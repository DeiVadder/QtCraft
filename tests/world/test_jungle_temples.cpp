#include "common/content/loader/QcBaseContent.h"
#include "common/core/QcIntMath.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QTest>

#include <algorithm>
#include <ctime>

using namespace Qt::StringLiterals;

// Jungle temples and witch huts (decisions E-620..E-622): surface structures of the jungle and the
// swamp, planned from the seed and the terrain noise alone (the same plans with a fresh generator,
// chunks the same in any order), only in their biome. A temple's chests and dispensers get loot; a
// hut stands on stilts, also over water up to its rule's depth, with the water under its platform
// kept, and its inside is the witch's guardian zone.
class TestJungleTemples : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 513;
    static constexpr int regions = 8;
    static constexpr int spacing = 256;
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;

    std::shared_ptr<const QcContentRegistry> m_content = []() {
        QStringList warnings;
        return *QcBaseContent::load(warnings);
    }();
    std::unique_ptr<QcTerrainGenerator> m_terrain;
    int m_temple = -1;
    int m_hut = -1;
    QList<std::shared_ptr<const QcStructurePlacement>> m_temples;
    QList<std::shared_ptr<const QcStructurePlacement>> m_huts;

    static QList<std::shared_ptr<const QcStructurePlacement>> plans(const QcTerrainGenerator &terrain, int kind)
    {
        QList<std::shared_ptr<const QcStructurePlacement>> found;
        for (int rz = -regions; rz <= regions; ++rz) {
            for (int rx = -regions; rx <= regions; ++rx) {
                if (std::shared_ptr<const QcStructurePlacement> plan = terrain.surfaceStructures().planOf(kind, rx, rz)) {
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

    static void verifyWritten(const QcStructurePlacement &plan, const Chunks &chunks)
    {
        const QcStructure &structure = plan.structure();
        for (int y = 0; y < structure.sizeY(); ++y) {
            for (int z = 0; z < structure.sizeZ(); ++z) {
                for (int x = 0; x < structure.sizeX(); ++x) {
                    if (const std::optional<QcBlockId> block = plan.worldBlock({x, y, z})) {
                        QCOMPARE(at(chunks, plan.toWorld(QcBlockPos{x, y, z})), *block);
                    }
                }
            }
        }
    }

    // A fresh generator plans the same in the other order of the regions; each copy lies wholly in
    // its region, with the rule's biome along its edges and in its middle.
    void verifyPlans(const QList<std::shared_ptr<const QcStructurePlacement>> &found, int kind, const QString &biome) const
    {
        const QcTerrainGenerator fresh(m_content, seed);
        for (qsizetype i = found.size() - 1; i >= 0; --i) {
            const QcStructurePlacement &plan = *found[i];
            const int rx = qcFloorDiv(plan.origin().x, spacing);
            const int rz = qcFloorDiv(plan.origin().z, spacing);
            const std::shared_ptr<const QcStructurePlacement> again = fresh.surfaceStructures().planOf(kind, rx, rz);
            QVERIFY(again);
            QCOMPARE(again->origin(), plan.origin());
            QCOMPARE(again->turns(), plan.turns());
            QCOMPARE(qcFloorDiv(plan.origin().x + plan.sizeX() - 1, spacing), rx);
            QCOMPARE(qcFloorDiv(plan.origin().z + plan.sizeZ() - 1, spacing), rz);
            for (const auto &[dx, dz] : {std::pair{0, 0}, {plan.sizeX() - 1, plan.sizeZ() - 1}, {plan.sizeX() / 2, plan.sizeZ() / 2}, {0, plan.sizeZ() - 1},
                                         {plan.sizeX() - 1, 0}}) {
                QCOMPARE(m_terrain->biomeAt(plan.origin().x + dx, plan.origin().z + dz), biome);
            }
        }
    }

    static double threadSeconds()
    {
        timespec now{};
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
        return double(now.tv_sec) + double(now.tv_nsec) / 1e9;
    }

    // The content without the surface rules of the two kinds (for the measurement).
    std::shared_ptr<QcContentRegistry> contentWithout() const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        for (const QString &name : {u"qtcraft:jungle_temple"_s, u"qtcraft:witch_hut"_s}) {
            QcStructureDefinition structure = *content->structure(name);
            structure.surface.reset();
            content->registerStructure(structure);
        }
        return content;
    }

private slots:
    void initTestCase()
    {
        m_terrain = std::make_unique<QcTerrainGenerator>(m_content, seed);
        const std::optional<int> temple = m_terrain->surfaceStructures().planner().kindIndex(u"qtcraft:jungle_temple"_s);
        const std::optional<int> hut = m_terrain->surfaceStructures().planner().kindIndex(u"qtcraft:witch_hut"_s);
        QVERIFY(temple && hut);
        m_temple = *temple;
        m_hut = *hut;
        m_temples = plans(*m_terrain, m_temple);
        m_huts = plans(*m_terrain, m_hut);
        qInfo().noquote() << u"%1 jungle temples and %2 witch huts in %3 regions"_s.arg(m_temples.size()).arg(m_huts.size()).arg((2 * regions + 1) * (2 * regions + 1));
        QVERIFY(m_temples.size() >= 2);
        QVERIFY(m_huts.size() >= 2);
    }

    void plansAreDeterministicAndInTheirBiome()
    {
        verifyPlans(m_temples, m_temple, u"qtcraft:jungle"_s);
        verifyPlans(m_huts, m_hut, u"qtcraft:swamp"_s);
        // Another seed plans elsewhere.
        const QcTerrainGenerator other(m_content, seed + 1);
        const QList<std::shared_ptr<const QcStructurePlacement>> otherTemples = plans(other, m_temple);
        QVERIFY(otherTemples.isEmpty() || otherTemples.front()->origin() != m_temples.front()->origin());
    }

    void templeChunksAreTheSameInAnyOrder()
    {
        const QcStructurePlacement &plan = *m_temples.front();
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

        // The cellar below the ground with its mechanism at rest: the door shut, the torch lit, the
        // pistons out; cleared above the roof.
        QCOMPARE(plan.structure().groundLayer, 3);
        QCOMPARE(at(first, plan.toWorld(QcBlockPos{7, plan.structure().sizeY() + 1, 8})), qcAirBlockId);
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{5, 1, 5})), u"qtcraft:mossy_cobblestone"_s);
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{6, 1, 3})), u"qtcraft:signal_torch"_s);
        QCOMPARE(m_content->property(at(first, plan.toWorld(QcBlockPos{5, 2, 3})), u"extended"_s), u"true"_s);
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{11, 1, 11})), u"qtcraft:stone_pressure_plate"_s);
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{11, 2, 14})), u"qtcraft:ladder"_s);
    }

    void templeChestsAndDispensersGetLoot()
    {
        const QcStructurePlacement &plan = *m_temples.front();
        int chests = 0;
        int dispensers = 0;
        const QList<QcChunkPos> planChunks = chunksOf(plan);
        for (const QcChunkPos &pos : planChunks) {
            const QcChunkFurnishing furnishing = m_terrain->furnishingIn(pos);
            chests += int(std::ranges::count(furnishing.chests, u"qtcraft:jungle_temple_chest"_s, &QcChunkFurnishing::Chest::lootTable));
            dispensers += int(std::ranges::count(furnishing.chests, u"qtcraft:jungle_temple_dispenser"_s, &QcChunkFurnishing::Chest::lootTable));
        }
        QCOMPARE(chests, 2);
        QCOMPARE(dispensers, 3);
        QVERIFY(m_content->lootTables().find(u"qtcraft:jungle_temple_chest"_s));
        QVERIFY(m_content->lootTables().find(u"qtcraft:jungle_temple_dispenser"_s));
        // A temple has no guardian zone.
        const QcBlockPos middle = plan.toWorld(QcBlockPos{8, 1, 6});
        const QList<QcGuardSite> sites = m_terrain->guardSitesNear(middle, 16);
        QVERIFY(std::ranges::none_of(sites, [&](const QcGuardSite &site) { return site.contains(middle); }));
    }

    void hutsStandOnStiltsAlsoOverWater()
    {
        // The rule lets the samples lie under up to three blocks of water; some huts stand over it.
        int overWater = 0;
        for (const std::shared_ptr<const QcStructurePlacement> &plan : std::as_const(m_huts)) {
            // The platform above the water's surface (the terrain's water reaches up to the sea
            // level, exclusive).
            const int platform = plan->origin().y + 4;
            QVERIFY(platform >= m_terrain->seaLevel());
            bool wet = false;
            for (int dz = 0; dz < plan->sizeZ(); dz += 2) {
                for (int dx = 0; dx < plan->sizeX(); dx += 2) {
                    const QcTerrainColumn column = m_terrain->column(plan->origin().x + dx, plan->origin().z + dz);
                    wet = wet || column.height < m_terrain->seaLevel();
                }
            }
            // The corners are samples of the rule: under at most three blocks of water.
            for (const auto &[dx, dz] : {std::pair{0, 0}, {plan->sizeX() - 1, 0}, {0, plan->sizeZ() - 1}, {plan->sizeX() - 1, plan->sizeZ() - 1}}) {
                QVERIFY(m_terrain->seaLevel() - m_terrain->column(plan->origin().x + dx, plan->origin().z + dz).height <= 3);
            }
            overWater += wet ? 1 : 0;
        }
        qInfo().noquote() << u"%1 of %2 huts stand partly over water"_s.arg(overWater).arg(m_huts.size());
        QVERIFY(overWater >= 1);

        // A hut over water: written in any order of its chunks, the water under its platform kept,
        // the stilts down to the ground.
        const auto wetPlan = std::ranges::find_if(std::as_const(m_huts), [this](const std::shared_ptr<const QcStructurePlacement> &plan) {
            const QcTerrainColumn column = m_terrain->column(plan->origin().x + 3, plan->origin().z + 4);
            return column.height < m_terrain->seaLevel();
        });
        const QcStructurePlacement &plan = wetPlan != m_huts.cend() ? **wetPlan : *m_huts.front();
        QList<QcChunkPos> positions = chunksOf(plan);
        const Chunks first = generate(QcTerrainGenerator(m_content, seed), positions);
        std::ranges::reverse(positions);
        const Chunks second = generate(QcTerrainGenerator(m_content, seed), positions);
        for (const QcChunkPos &pos : std::as_const(positions)) {
            for (int y = plan.origin().y - 4; y < plan.origin().y + plan.structure().sizeY() + 6; ++y) {
                for (int z = 0; z < QcWorld::chunkSize; ++z) {
                    for (int x = 0; x < QcWorld::chunkSize; ++x) {
                        QCOMPARE(second[pos]->block(x, y, z), first[pos]->block(x, y, z));
                    }
                }
            }
        }
        verifyWritten(plan, first);
        const QcTerrainColumn middle = m_terrain->column(plan.toWorld(QcBlockPos{3, 0, 4}).x, plan.toWorld(QcBlockPos{3, 0, 4}).z);
        if (middle.height < m_terrain->seaLevel()) {
            const QcBlockPos underPlatform = plan.toWorld(QcBlockPos{3, 0, 4});
            QCOMPARE(nameAt(first, {underPlatform.x, m_terrain->seaLevel() - 1, underPlatform.z}), u"qtcraft:water"_s);
        }
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{1, 0, 2})), u"qtcraft:oak_log"_s);
        QCOMPARE(nameAt(first, plan.toWorld(QcBlockPos{4, 5, 6})), u"qtcraft:cauldron"_s);

        // Its inside is the witch's guardian zone, on the platform.
        const QcBlockPos inside = plan.toWorld(QcBlockPos{3, 5, 4});
        const QList<QcGuardSite> sites = m_terrain->guardSitesNear(inside, 32);
        const auto site = std::ranges::find_if(sites, [&](const QcGuardSite &s) { return s.contains(inside); });
        QVERIFY(site != sites.cend());
        QCOMPARE(site->rule.mob, u"qtcraft:witch"_s);
        QCOMPARE(site->min.y, plan.origin().y + 5);
    }

    void measureGenerationCost()
    {
        const auto without = contentWithout();
        const auto seconds = [](const QcTerrainGenerator &terrain, const QList<QcChunkPos> &chunks) {
            const double start = threadSeconds();
            for (const QcChunkPos &pos : chunks) {
                terrain.generate(pos);
            }
            return threadSeconds() - start;
        };

        // The chunks of two temples and two huts, fresh generators interleaved (plans made afresh).
        QList<QcChunkPos> chunks;
        for (qsizetype i = 0; i < 2; ++i) {
            chunks.append(chunksOf(*m_temples[i]));
            chunks.append(chunksOf(*m_huts[i]));
        }
        double withTime = 0.0;
        double plainTime = 0.0;
        for (int round = 0; round < 2; ++round) {
            plainTime += seconds(QcTerrainGenerator(without, seed), chunks);
            withTime += seconds(QcTerrainGenerator(m_content, seed), chunks);
        }
        const double count = 2.0 * double(chunks.size());
        qInfo().noquote() << u"chunks/s of %1 temple and hut chunks without them %2, with %3"_s.arg(chunks.size())
                                 .arg(count / plainTime, 0, 'f', 1)
                                 .arg(count / withTime, 0, 'f', 1);

        // An area around a temple with all kinds planned afresh.
        QList<QcChunkPos> area;
        const QcChunkPos corner = m_temples.front()->origin().chunk();
        for (int cz = -6; cz < 6; ++cz) {
            for (int cx = -6; cx < 6; ++cx) {
                area.append({corner.x + cx, corner.z + cz});
            }
        }
        const double a = seconds(QcTerrainGenerator(without, seed), area);
        const double b = seconds(QcTerrainGenerator(m_content, seed), area);
        qInfo().noquote() << u"chunks/s of %1 chunks around a temple without the two kinds %2, with %3"_s.arg(area.size())
                                 .arg(double(area.size()) / a, 0, 'f', 1)
                                 .arg(double(area.size()) / b, 0, 'f', 1);
    }
};

QTEST_GUILESS_MAIN(TestJungleTemples)
#include "test_jungle_temples.moc"
