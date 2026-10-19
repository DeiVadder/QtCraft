#include "QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QElapsedTimer>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

// Cost of villages in world generation (decision E-403, E-407, E-409): the heap a plan holds; the chunks of the 32 x 32 chunks
// around a village (a first one and a swamp village) and of the same area of a world without villages (the village chance 0), generated on
// one thread; the planning of the regions around a point as /village and /locate village do it.
// A measurement, not a check: set QTCRAFT_VILLAGE_MEASURE=1 and use a Release build. Without it
// the test only runs a small area once.
class TestVillageMeasure : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    static bool full() { return qEnvironmentVariableIsSet("QTCRAFT_VILLAGE_MEASURE"); }

    std::shared_ptr<const QcContentRegistry> withoutVillages() const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcVillageSettings settings = content->villageSettings();
        settings.chance = 0.0;
        content->setVillageSettings(settings);
        return content;
    }

    // Milliseconds per chunk over the square of `edge` chunks around `centre`, the fastest of `runs`.
    // Empty when the generator gives no chunk.
    static std::optional<double> perChunk(const std::shared_ptr<const QcContentRegistry> &content, QcChunkPos centre, int edge, int runs)
    {
        double best = 1e9;
        for (int run = 0; run < runs; ++run) {
            const QcTerrainGenerator generator(content, seed);
            QElapsedTimer timer;
            timer.start();
            for (int dz = 0; dz < edge; ++dz) {
                for (int dx = 0; dx < edge; ++dx) {
                    if (!generator.generate({centre.x - edge / 2 + dx, centre.z - edge / 2 + dz})) {
                        return std::nullopt;
                    }
                }
            }
            best = std::min(best, double(timer.nsecsElapsed()) / 1e6 / double(edge * edge));
        }
        return best;
    }

    // The first village of the style in the regions around the origin, ring by ring.
    static std::shared_ptr<const QcVillagePlan> firstOf(const QcTerrainGenerator &generator, const QString &style)
    {
        for (int ring = 0; ring <= 14; ++ring) {
            for (int rz = -ring; rz <= ring; ++rz) {
                for (int rx = -ring; rx <= ring; ++rx) {
                    if (std::max(std::abs(rx), std::abs(rz)) != ring) {
                        continue;
                    }
                    std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz);
                    if (plan && plan->style == style) {
                        return plan;
                    }
                }
            }
        }
        return nullptr;
    }

    void measureAround(const QcVillagePlan &plan) const
    {
        const int edge = full() ? 32 : 6;
        const int runs = full() ? 3 : 1;
        const QcChunkPos centre = plan.centre.chunk();
        const std::optional<double> around = perChunk(m_content, centre, edge, runs);
        const std::optional<double> aroundWithout = perChunk(withoutVillages(), centre, edge, runs);
        // Only the 8 x 8 chunks of the village itself.
        const std::optional<double> village = perChunk(m_content, centre, 8, runs);
        const std::optional<double> villageWithout = perChunk(withoutVillages(), centre, 8, runs);
        QVERIFY(around && aroundWithout && village && villageWithout);

        const double with = *around;
        const double without = *aroundWithout;
        const double villageOnly = *village;
        const double villageOnlyWithout = *villageWithout;
        qInfo("%s: %d x %d chunks around the village at %d %d: %.3f ms per chunk with villages, %.3f without (%+.1f %%)",
              qPrintable(plan.style), edge, edge, plan.centre.x, plan.centre.z, with, without, (with / without - 1.0) * 100.0);
        qInfo("%s: 8 x 8 chunks of the village: %.3f ms per chunk with, %.3f without (%+.1f %%)", qPrintable(plan.style), villageOnly,
              villageOnlyWithout, (villageOnly / villageOnlyWithout - 1.0) * 100.0);
    }

    // Bytes of the heap a list holds.
    template<typename T>
    static qsizetype listBytes(const QList<T> &list)
    {
        return list.capacity() * qsizetype(sizeof(T));
    }

    // What one structure of a plan holds: its cells, palette and the palette's index.
    static qsizetype structureBytes(const QcStructure &structure)
    {
        const qsizetype cells = qsizetype(structure.sizeX()) * structure.sizeY() * structure.sizeZ();
        const qsizetype palette = structure.palette().size();
        // QHash<QcBlockId, qint16>: about 16 bytes per entry with its spans.
        return qsizetype(sizeof(QcStructure)) + cells * qsizetype(sizeof(qint16)) + palette * qsizetype(sizeof(QcBlockId)) + palette * 16;
    }

    struct PlanBytes
    {
        qsizetype structures = 0;
        qsizetype placements = 0;
        qsizetype cells = 0;
        qsizetype other = 0;

        qsizetype total() const { return structures + placements + cells + other; }
    };

    // The heap a plan holds; structures the styles hold too are not counted.
    static PlanBytes bytesOf(const QcVillagePlan &plan, const QSet<const QcStructure *> &shared)
    {
        PlanBytes bytes;
        QSet<const QcStructure *> own;
        for (const QcStructurePlacement &piece : plan.pieces) {
            const QcStructure *structure = &piece.structure();
            if (!shared.contains(structure) && !own.contains(structure)) {
                own.insert(structure);
                bytes.structures += structureBytes(*structure);
            }
            bytes.placements += structure->palette().size() * qsizetype(sizeof(QcBlockId));
        }
        bytes.placements += listBytes(plan.pieces);
        bytes.cells = listBytes(plan.roads) + listBytes(plan.branchRoads) + listBytes(plan.thresholds) + listBytes(plan.steps)
                      + listBytes(plan.decks) + listBytes(plan.runs);
        bytes.other = qsizetype(sizeof(QcVillagePlan)) + listBytes(plan.beds) + listBytes(plan.workstations) + listBytes(plan.mobs)
                      + listBytes(plan.wetland.wet) + listBytes(plan.wetland.dry);
        return bytes;
    }

    // The structures the styles of the generator hold.
    static QSet<const QcStructure *> styleStructures(const QcTerrainGenerator &generator, const QcContentRegistry &content)
    {
        QSet<const QcStructure *> shared;
        for (const QcVillageStyleDefinition &definition : content.villageStyles()) {
            const QcVillageStyle *style = generator.villages().styles().named(definition.name);
            if (!style) {
                continue;
            }
            QList<QcVillageStyle::Piece> pieces = style->pieces;
            pieces.append(style->centre);
            pieces.append(style->lamp);
            for (const QcVillageStyle::Piece &piece : std::as_const(pieces)) {
                for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
                    shared.insert(blueprint.structure().get());
                }
            }
        }
        return shared;
    }

private slots:
    void listOfVillages()
    {
        // For screenshots and reviews: QTCRAFT_VILLAGE_LIST=<rings> lists the villages of the regions
        // within that many rings of the origin (style, street plan, winding, centre, box, pieces).
        const int rings = qEnvironmentVariableIntValue("QTCRAFT_VILLAGE_LIST");
        if (rings <= 0) {
            QSKIP("set QTCRAFT_VILLAGE_LIST to list villages");
        }
        const QcTerrainGenerator generator(m_content, seed);
        for (int rz = -rings; rz <= rings; ++rz) {
            for (int rx = -rings; rx <= rings; ++rx) {
                if (const std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz)) {
                    qInfo("V %s %s w%d c %d %d %d box %d %d %d %d len %d pieces %lld", qPrintable(plan->style), qPrintable(plan->layout),
                          plan->winding ? 1 : 0, plan->centre.x, plan->centre.y, plan->centre.z, plan->minX, plan->minZ, plan->maxX,
                          plan->maxZ, plan->roadLength, qint64(plan->pieces.size()));
                }
            }
        }
    }

    void memoryOfThePlans()
    {
        // The plan cache (QcVillageGenerator::cachedPlans) holds plans and empty regions alike.
        const QcTerrainGenerator generator(m_content, seed);
        const int rings = full() ? 12 : 3;
        const QSet<const QcStructure *> shared = styleStructures(generator, *m_content);
        int regions = 0;
        int villages = 0;
        PlanBytes sum;
        qsizetype largest = 0;
        for (int rz = -rings; rz <= rings; ++rz) {
            for (int rx = -rings; rx <= rings; ++rx) {
                ++regions;
                const std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz);
                if (!plan) {
                    continue;
                }
                ++villages;
                const PlanBytes bytes = bytesOf(*plan, shared);
                sum.structures += bytes.structures;
                sum.placements += bytes.placements;
                sum.cells += bytes.cells;
                sum.other += bytes.other;
                largest = std::max(largest, bytes.total());
            }
        }
        QVERIFY(villages > 0);
        const double perPlan = double(sum.total()) / villages;
        const double cache = perPlan * double(villages) / double(regions) * double(QcVillageGenerator::cachedPlans);
        qInfo("plans: %d villages in %d regions, %.1f KB per village (structures %.1f, placements %.1f, road cells %.1f, other %.1f), "
              "largest %.1f KB; a full cache of %lld regions about %.1f MB",
              villages, regions, perPlan / 1024.0, double(sum.structures) / villages / 1024.0, double(sum.placements) / villages / 1024.0,
              double(sum.cells) / villages / 1024.0, double(sum.other) / villages / 1024.0, double(largest) / 1024.0,
              qint64(QcVillageGenerator::cachedPlans), cache / 1024.0 / 1024.0);
    }

    void generationWithAndWithoutVillages()
    {
        const QcTerrainGenerator probe(m_content, seed);
        std::shared_ptr<const QcVillagePlan> plan;
        for (int r = 0; r < 6 && !plan; ++r) {
            plan = probe.villages().regionPlan(r, 0);
        }
        QVERIFY(plan);
        measureAround(*plan);
    }

    void generationOfASwampVillage()
    {
        // Over water: stilts, boardwalks and the shaped wetland (decision E-407).
        const QcTerrainGenerator probe(m_content, seed);
        const std::shared_ptr<const QcVillagePlan> plan = firstOf(probe, u"qtcraft:swamp"_s);
        QVERIFY(plan);
        measureAround(*plan);
    }

    void planningAroundAPoint()
    {
        // /village and /locate village plan up to 13 x 13 regions on the server thread.
        const int runs = full() ? 5 : 1;
        double best = 1e9;
        double cached = 1e9;
        for (int run = 0; run < runs; ++run) {
            const QcTerrainGenerator generator(m_content, seed);
            QElapsedTimer timer;
            timer.start();
            QVERIFY(generator.nearestVillage({0, 64, 0}, 2048));
            best = std::min(best, double(timer.nsecsElapsed()) / 1e6);
            timer.restart();
            QVERIFY(generator.nearestVillage({0, 64, 0}, 2048));
            cached = std::min(cached, double(timer.nsecsElapsed()) / 1e6);
        }
        qInfo("nearest village within 2048 blocks: %.1f ms first, %.3f ms planned already", best, cached);
    }

    void planningOneVillage()
    {
        // What a plan the cache let go of costs the server thread when it is planned again
        // (decision E-416: the plans of villages with loaded chunks are held, never planned again).
        const QcTerrainGenerator probe(m_content, seed);
        const std::shared_ptr<const QcVillagePlan> found = probe.nearestVillage({0, 64, 0}, 2048);
        QVERIFY(found);
        const int spacing = probe.villages().spacing();
        const int regionX = qcFloorDiv(found->centre.x, spacing);
        const int regionZ = qcFloorDiv(found->centre.z, spacing);
        double best = 1e9;
        for (int run = 0; run < (full() ? 5 : 1); ++run) {
            const QcTerrainGenerator generator(m_content, seed);
            QElapsedTimer timer;
            timer.start();
            QVERIFY(generator.villages().regionPlan(regionX, regionZ));
            best = std::min(best, double(timer.nsecsElapsed()) / 1e6);
        }
        qInfo("one village planned anew: %.2f ms", best);
    }
};

QTEST_GUILESS_MAIN(TestVillageMeasure)
#include "test_village_measure.moc"
