// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcTestUtil.h"
#include "worldgen/surface/QcSurfaceBlend.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// The temples on the surface stand on their floor, not sunk into the slope (decision E-889): the
// ground around a temple is blended to its floor (QcSurfaceStructures, the rule's `blend`), so
// every opening of its walls at the floor leads out into the open, whichever side faces uphill,
// and the ground right beside its walls lies no higher than its floor. Fixed seeds plus one random
// seed per run (decision E-846).
class TestTempleEntrances : public QObject
{
    Q_OBJECT

private:
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;
    // Regions searched per kind and seed (both ways from region 0), and temples checked per seed.
    static constexpr int regions = 3;
    static constexpr int maxTemples = 4;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    const qint64 m_randomSeed = QcTestUtil::randomSeed();

    QList<std::shared_ptr<const QcStructurePlacement>> templesOf(const QcTerrainGenerator &terrain, int kind) const
    {
        QList<std::shared_ptr<const QcStructurePlacement>> found;
        for (int rz = -regions; rz <= regions && found.size() < maxTemples; ++rz) {
            for (int rx = -regions; rx <= regions && found.size() < maxTemples; ++rx) {
                if (std::shared_ptr<const QcStructurePlacement> plan = terrain.surfaceStructures().planOf(kind, rx, rz)) {
                    found.append(std::move(plan));
                }
            }
        }
        return found;
    }

    // The chunks of the footprint and one block around it.
    static Chunks chunksAround(const QcTerrainGenerator &terrain, const QcStructurePlacement &plan)
    {
        Chunks chunks;
        const QcChunkPos first = plan.origin().offset(-1, 0, -1).chunk();
        const QcChunkPos last = plan.origin().offset(plan.sizeX(), 0, plan.sizeZ()).chunk();
        for (int cx = first.x; cx <= last.x; ++cx) {
            for (int cz = first.z; cz <= last.z; ++cz) {
                chunks.insert({cx, cz}, terrain.generate({cx, cz}));
            }
        }
        return chunks;
    }

    bool solidAt(const Chunks &chunks, QcBlockPos pos) const
    {
        const std::shared_ptr<QcChunk> chunk = chunks.value(pos.chunk());
        if (!chunk || pos.y < 0 || pos.y >= QcWorld::worldHeight) {
            return false;
        }
        const QcBlockDefinition &block = m_content->block(chunk->block(pos.localX(), pos.y, pos.localZ()));
        return block.solid && !block.liquid;
    }

    // The world column just outside the footprint next to a border column, if `pos` is one.
    static std::optional<QcBlockPos> outside(const QcStructurePlacement &plan, QcBlockPos pos)
    {
        const QcBlockPos origin = plan.origin();
        if (pos.x == origin.x) {
            return pos.offset(-1, 0, 0);
        }
        if (pos.x == origin.x + plan.sizeX() - 1) {
            return pos.offset(1, 0, 0);
        }
        if (pos.z == origin.z) {
            return pos.offset(0, 0, -1);
        }
        if (pos.z == origin.z + plan.sizeZ() - 1) {
            return pos.offset(0, 0, 1);
        }
        return std::nullopt;
    }

    struct Check
    {
        int openings = 0;
        int blocked = 0;
        // The highest ground beside the walls above the floor, in blocks.
        int buried = 0;
    };

    // The openings of the walls at the floor (two air cells one over the other on the border, one
    // layer over the ground layer) and the ground beside every wall column.
    Check check(const QcStructurePlacement &plan, const Chunks &chunks) const
    {
        Check result;
        const QcStructure &structure = plan.structure();
        const int layer = structure.groundLayer + 1;
        const int floor = plan.origin().y + structure.groundLayer;
        for (int z = 0; z < structure.sizeZ(); ++z) {
            for (int x = 0; x < structure.sizeX(); ++x) {
                const QcBlockPos world = plan.toWorld(QcBlockPos{x, layer, z});
                const std::optional<QcBlockPos> beside = outside(plan, world);
                if (!beside) {
                    continue;
                }
                int top = floor;
                while (top < floor + 8 && solidAt(chunks, {beside->x, top + 1, beside->z})) {
                    ++top;
                }
                result.buried = std::max(result.buried, top - floor);

                const bool open = plan.worldBlock({x, layer, z}) == qcAirBlockId && plan.worldBlock({x, layer + 1, z}) == qcAirBlockId;
                if (open) {
                    ++result.openings;
                    if (solidAt(chunks, *beside) || solidAt(chunks, beside->offset(0, 1, 0))) {
                        ++result.blocked;
                    }
                }
            }
        }
        return result;
    }

    void verifyKind(const QString &name, const QList<qint64> &seeds)
    {
        int temples = 0;
        for (const qint64 seed : seeds) {
            const QcTerrainGenerator terrain(m_content, seed);
            const std::optional<int> kind = terrain.surfaceStructures().planner().kindIndex(name);
            QVERIFY(kind);
            const QList<std::shared_ptr<const QcStructurePlacement>> plans = templesOf(terrain, *kind);
            for (const std::shared_ptr<const QcStructurePlacement> &plan : plans) {
                const Check result = check(*plan, chunksAround(terrain, *plan));
                const QcBlockPos at = plan->origin();
                qInfo().noquote() << u"%1 seed %2 at %3 %4 %5: %6 openings, %7 blocked, ground beside the walls up to %8 over the floor"_s.arg(name)
                                         .arg(seed)
                                         .arg(at.x)
                                         .arg(at.y)
                                         .arg(at.z)
                                         .arg(result.openings)
                                         .arg(result.blocked)
                                         .arg(result.buried);
                QVERIFY2(result.openings > 0, "a temple has an entrance");
                QCOMPARE(result.blocked, 0);
                QCOMPARE(result.buried, 0);
                ++temples;
            }
        }
        QVERIFY2(temples >= 3, "the seeds have temples to check");
    }

    struct Placed
    {
        std::shared_ptr<const QcStructurePlacement> plan;
        QcSurfacePlanner::Rect region;
        int blend = 0;
    };

    // The copies of every surface kind in the regions around the origin, each with its region.
    static QList<Placed> placedOf(const QcTerrainGenerator &terrain)
    {
        QList<Placed> placed;
        const QList<QcSurfacePlanner::Kind> &kinds = terrain.surfaceStructures().planner().kinds();
        for (qsizetype kind = 0; kind < kinds.size(); ++kind) {
            const int spacing = kinds[kind].rule.spacing;
            for (int rz = -regions; rz <= regions; ++rz) {
                for (int rx = -regions; rx <= regions; ++rx) {
                    std::shared_ptr<const QcStructurePlacement> plan = terrain.surfaceStructures().planOf(static_cast<int>(kind), rx, rz);
                    if (plan) {
                        const QcSurfacePlanner::Rect region{rx * spacing, rz * spacing, (rx + 1) * spacing - 1, (rz + 1) * spacing - 1};
                        placed.append({std::move(plan), region, kinds[kind].rule.blend});
                    }
                }
            }
        }
        return placed;
    }

    static QcSurfacePlanner::Rect blended(const Placed &copy)
    {
        const QcSurfacePlanner::Rect footprint = QcSurfacePlanner::footprintOf(*copy.plan);
        return {footprint.x0 - copy.blend, footprint.z0 - copy.blend, footprint.x1 + copy.blend, footprint.z1 + copy.blend};
    }

    // The road cells within the blend around a village's centre that are not the top of their
    // generated column (decision E-893: the roads lie on the blended ground): the cell solid, above
    // it nothing but air or the step of a rising road. A cave that opens the ground more than a
    // block below a cell leaves a hole (the road builder skips it, as anywhere in a village).
    int roadsOffTheGround(const QcTerrainGenerator &terrain, const QcVillagePlan &plan) const
    {
        Chunks chunks;
        int off = 0;
        int holes = 0;
        int checked = 0;
        const QList<QcBlockPos> cells = plan.roads + plan.branchRoads;
        for (const QcBlockPos &cell : cells) {
            const double distance = QcSurfaceBlend::distanceTo(plan.pieces.front(), cell.x, cell.z);
            if (distance == 0.0 || distance > plan.centreBlend) {
                continue;
            }
            ++checked;
            if (!chunks.contains(cell.chunk())) {
                chunks.insert(cell.chunk(), terrain.generate(cell.chunk()));
            }
            const QcChunk &chunk = *chunks.value(cell.chunk());
            const QcBlockId step = chunk.block(cell.localX(), cell.y + 1, cell.localZ());
            const bool stepOrAir = step == qcAirBlockId || m_content->block(step).name.endsWith(u"_slab"_s);
            const bool open = stepOrAir && chunk.block(cell.localX(), cell.y + 2, cell.localZ()) == qcAirBlockId;
            if (open && !solidAt(chunks, cell) && !solidAt(chunks, cell.offset(0, -1, 0))) {
                ++holes;
            } else if (!open || !solidAt(chunks, cell)) {
                qInfo().noquote() << u"road cell %1 %2 %3 is not the top of its column"_s.arg(cell.x).arg(cell.y).arg(cell.z);
                ++off;
            }
        }
        qInfo().noquote() << u"%1 road cells on the blended ground, %2 of them over a cave mouth"_s.arg(checked).arg(holes);
        return off;
    }

private slots:
    // A copy's footprint and its blended ground lie wholly inside its region, so a neighbour's
    // blend never cuts into its walls; nor does the blend of any other copy (decision E-889).
    void blendedGroundNeverReachesAnotherCopy()
    {
        int checked = 0;
        for (const qint64 seed : {qint64{51}, qint64{513}, m_randomSeed}) {
            const QcTerrainGenerator terrain(m_content, seed);
            const QList<Placed> placed = placedOf(terrain);
            for (const Placed &copy : placed) {
                const QcSurfacePlanner::Rect ground = blended(copy);
                QVERIFY(ground.x0 >= copy.region.x0 && ground.z0 >= copy.region.z0 && ground.x1 <= copy.region.x1 && ground.z1 <= copy.region.z1);
                for (const Placed &other : placed) {
                    if (other.plan != copy.plan) {
                        QVERIFY2(!ground.intersects(QcSurfacePlanner::footprintOf(*other.plan), 0), qPrintable(copy.plan->structure().name()));
                    }
                }
                checked += copy.blend > 0 ? 1 : 0;
            }
        }
        QVERIFY2(checked >= 3, "the seeds have copies with blended ground");
    }

    // A desert temple as the centre of a desert village (decision E-592, placed by the village, not
    // the surface planner) stands on its floor too: the village blends the ground around it
    // (decision E-893).
    void villageTemplesStandOnTheirFloor()
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcVillageStyleDefinition desert = *std::ranges::find(content->villageCatalog().styles(), u"qtcraft:desert"_s, &QcVillageStyleDefinition::name);
        desert.rareCentreChance = 1.0;
        content->villageCatalog().registerStyle(desert);
        int temples = 0;
        int sunk = 0;
        // Road cells on the blended ground around the temple that do not lie at its top.
        int roadsOff = 0;
        // 961302756 and 691287566: the blended ground lowered onto a cave's roof, and the road cut
        // into the ground lay in the cave, unbuilt, under the ground left above it (decision E-907).
        for (const qint64 seed : {qint64{51}, qint64{10}, qint64{1234}, qint64{961302756}, qint64{691287566}, m_randomSeed}) {
            const QcTerrainGenerator terrain(content, seed);
            for (int rz = -4; rz <= 4; ++rz) {
                for (int rx = -4; rx <= 4; ++rx) {
                    const std::shared_ptr<const QcVillagePlan> plan = terrain.villages().regionPlan(rx, rz);
                    if (!plan || plan->pieces.front().structure().name() != u"qtcraft:desert_temple"_s) {
                        continue;
                    }
                    const QcStructurePlacement &temple = plan->pieces.front();
                    const Check result = check(temple, chunksAround(terrain, temple));
                    qInfo().noquote() << u"village temple seed %1 at %2 %3 %4: %5 openings, %6 blocked, ground beside the walls up to %7 over the floor"_s
                                             .arg(seed)
                                             .arg(temple.origin().x)
                                             .arg(temple.origin().y)
                                             .arg(temple.origin().z)
                                             .arg(result.openings)
                                             .arg(result.blocked)
                                             .arg(result.buried);
                    QVERIFY2(result.openings > 0, "a temple has an entrance");
                    sunk += result.blocked > 0 || result.buried > 0 ? 1 : 0;
                    roadsOff += roadsOffTheGround(terrain, *plan);
                    ++temples;
                }
            }
        }
        QVERIFY2(temples >= 2, "the seeds have village temples to check");
        QCOMPARE(sunk, 0);
        QCOMPARE(roadsOff, 0);
    }

    // The ground's freedom ends at the blend's outer edge and rounds to the nearest block.
    void levelRoundsAtTheEdges()
    {
        const int floor = 60;
        QCOMPARE(QcSurfaceBlend::levelAt(floor, 65, 1.0, 8), floor); // 0.6 * 8 = 4.8 down: 60.2
        QCOMPARE(QcSurfaceBlend::levelAt(floor, 66, 1.0, 8), 61);
        QCOMPARE(QcSurfaceBlend::levelAt(floor, 55, 1.0, 8), floor);
        QCOMPARE(QcSurfaceBlend::levelAt(floor, 64, 8.0, 8), 63); // distance == blend: 0.6 left
        QCOMPARE(QcSurfaceBlend::levelAt(floor, 56, 8.0, 8), 57);
        QCOMPARE(QcSurfaceBlend::levelAt(floor, 64, 8.5, 8), 64);
        QCOMPARE(QcSurfaceBlend::levelAt(floor, 64, 9.0, 8), 64);
        QCOMPARE(QcSurfaceBlend::levelAt(floor, 64, 1.0, 0), 64); // no blend: no freedom outside
    }

    void distanceIsEuclideanToTheFootprint()
    {
        const QcTerrainGenerator terrain(m_content, 51);
        const std::optional<int> kind = terrain.surfaceStructures().planner().kindIndex(u"qtcraft:desert_temple"_s);
        QVERIFY(kind);
        const QList<std::shared_ptr<const QcStructurePlacement>> plans = templesOf(terrain, *kind);
        QVERIFY(!plans.isEmpty());
        const QcStructurePlacement &plan = *plans.constFirst();
        const QcBlockPos origin = plan.origin();
        const int east = origin.x + plan.sizeX() - 1;
        QCOMPARE(QcSurfaceBlend::distanceTo(plan, origin.x, origin.z), 0.0);
        QCOMPARE(QcSurfaceBlend::distanceTo(plan, east, origin.z + 1), 0.0);
        QCOMPARE(QcSurfaceBlend::distanceTo(plan, origin.x - 1, origin.z), 1.0);
        QCOMPARE(QcSurfaceBlend::distanceTo(plan, east + 8, origin.z), 8.0);
        QCOMPARE(QcSurfaceBlend::distanceTo(plan, origin.x - 3, origin.z - 4), 5.0);
        QCOMPARE(QcSurfaceBlend::distanceTo(plan, origin.x - 1, origin.z - 1), std::sqrt(2.0));
    }

    void desertTemplesStandOnTheirFloor() { verifyKind(u"qtcraft:desert_temple"_s, {51, 10, 1234, m_randomSeed}); }
    void jungleTemplesStandOnTheirFloor() { verifyKind(u"qtcraft:jungle_temple"_s, {513, 11, 42, m_randomSeed}); }
};

QTEST_GUILESS_MAIN(TestTempleEntrances)
#include "test_temple_entrances.moc"
