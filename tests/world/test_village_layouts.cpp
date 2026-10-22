#include "common/content/parser/QcVillageLayoutParser.h"
#include "QcTestUtil.h"
#include "world/QcVillageTestUtil.h"

#include <QHash>
#include <QQueue>
#include <QSet>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace QcVillageTestUtil;

// Street plans of villages (decision E-408): every style lays its villages out after one of its
// weighted layouts (cross, street, T, L, plaza with a ring road, tree with arms at angles), roads may
// wind with the terrain; pieces never overlap each other or a road, every piece is reachable from the
// centre over roads, and every layout comes out the same in any chunk order.
class TestVillageLayouts : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 seed = 42;
    // The statistics run over a fixed sample (decision E-424): the first 160 villages ring by ring
    // around the origin (about 31 x 31 regions at seed 42; the rings stop at 35 x 35).
    static constexpr qsizetype sampleSize = 160;
    static constexpr int rings = 17;

    using Plans = QList<std::shared_ptr<const QcVillagePlan>>;

    static qint64 key(int x, int z) { return (qint64(x) << 32) | quint32(z); }

    // The runs of each road (QcVillagePlan::Run::arm), in order.
    static QList<QList<QcVillagePlan::Run>> armsOf(const QcVillagePlan &plan)
    {
        QList<QList<QcVillagePlan::Run>> arms;
        for (const QcVillagePlan::Run &run : plan.runs) {
            if (arms.isEmpty() || arms.back().front().arm != run.arm) {
                arms.append(QList<QcVillagePlan::Run>{});
            }
            arms.back().append(run);
        }
        return arms;
    }

    // Main roads that start at the village centre (not on a ring or another road).
    static QList<QList<QcVillagePlan::Run>> armsFromCentre(const QcVillagePlan &plan)
    {
        const QcStructurePlacement &centre = plan.pieces.front();
        const int cx = centre.origin().x + centre.sizeX() / 2;
        const int cz = centre.origin().z + centre.sizeZ() / 2;
        QList<QList<QcVillagePlan::Run>> arms;
        const QList<QList<QcVillagePlan::Run>> all = armsOf(plan);
        for (const QList<QcVillagePlan::Run> &arm : all) {
            const QcVillagePlan::Run &first = arm.front();
            if (!first.branch && std::abs(first.x - cx) <= 1 && std::abs(first.z - cz) <= 1) {
                arms.append(arm);
            }
        }
        return arms;
    }

    static int lengthOf(const QList<QcVillagePlan::Run> &arm)
    {
        int length = 0;
        for (const QcVillagePlan::Run &run : arm) {
            length += run.length;
        }
        return length;
    }

    static bool isLamp(const QcStructurePlacement &piece) { return shortName(piece).endsWith(u"_lamp"_s); }

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcTerrainGenerator m_generator{m_content, seed};
    Plans m_plans = samplePlans(m_generator, sampleSize, rings);

    Plans plansOf(const QString &layout) const
    {
        Plans plans;
        for (const std::shared_ptr<const QcVillagePlan> &plan : m_plans) {
            if (plan->layout == layout) {
                plans.append(plan);
            }
        }
        return plans;
    }

    // Columns of all road cells (both ranks and boardwalks).
    static QSet<qint64> roadColumns(const QcVillagePlan &plan)
    {
        QSet<qint64> columns;
        for (const QList<QcBlockPos> *cells : {&plan.roads, &plan.branchRoads}) {
            for (const QcBlockPos &cell : *cells) {
                columns.insert(key(cell.x, cell.z));
            }
        }
        for (const QList<QcVillagePlan::Deck> *decks : {&plan.decks, &plan.bridges}) {
            for (const QcVillagePlan::Deck &deck : *decks) {
                columns.insert(key(deck.at.x, deck.at.z));
            }
        }
        return columns;
    }

    // The road cells reached from those beside the centre piece, four ways round.
    static QSet<qint64> reachedFromCentre(const QcVillagePlan &plan)
    {
        const QSet<qint64> roads = roadColumns(plan);
        const QcStructurePlacement &centre = plan.pieces.front();
        QSet<qint64> reached;
        QQueue<std::pair<int, int>> queue;
        const auto visit = [&](int x, int z) {
            if (roads.contains(key(x, z)) && !reached.contains(key(x, z))) {
                reached.insert(key(x, z));
                queue.enqueue({x, z});
            }
        };
        for (int z = centre.origin().z - 1; z <= centre.origin().z + centre.sizeZ(); ++z) {
            for (int x = centre.origin().x - 1; x <= centre.origin().x + centre.sizeX(); ++x) {
                visit(x, z);
            }
        }
        while (!queue.isEmpty()) {
            const auto [x, z] = queue.dequeue();
            visit(x + 1, z);
            visit(x - 1, z);
            visit(x, z + 1);
            visit(x, z - 1);
        }
        return reached;
    }

    // Whether a cell of `cells` lies within two blocks of the piece.
    static bool nearAny(const QcStructurePlacement &piece, const QSet<qint64> &cells)
    {
        for (int z = piece.origin().z - 2; z <= piece.origin().z + piece.sizeZ() + 1; ++z) {
            for (int x = piece.origin().x - 2; x <= piece.origin().x + piece.sizeX() + 1; ++x) {
                if (cells.contains(key(x, z))) {
                    return true;
                }
            }
        }
        return false;
    }

private slots:
    void eachLayoutAppearsWithItsWeight()
    {
        // Per style: the share of each kind against its weight. A plaza whose ring is cut becomes a
        // cross, so crosses may come out a little above their weight and plazas below.
        QCOMPARE(m_plans.size(), sampleSize);
        QHash<QString, QHash<QString, int>> counts;
        QHash<QString, int> totals;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            ++counts[plan->style][plan->layout];
            ++totals[plan->style];
        }
        const QcVillageStyles styles(*m_content);
        QSet<QString> seen;
        for (auto it = totals.cbegin(); it != totals.cend(); ++it) {
            const QcVillageStyle *style = styles.named(it.key());
            QVERIFY(style);
            int weights = 0;
            for (const QcVillageLayout &layout : style->layouts) {
                weights += layout.weight;
            }
            QStringList shares;
            for (const QcVillageLayout &layout : style->layouts) {
                const QString kind = QcVillageLayoutParser::kindName(layout.kind);
                const double share = double(counts[it.key()].value(kind)) / double(it.value());
                const double expected = double(layout.weight) / double(weights);
                shares.append(u"%1 %2/%3"_s.arg(kind).arg(share, 0, 'f', 2).arg(expected, 0, 'f', 2));
                seen.insert(kind);
                // Only styles with at least 60 villages in the sample. The tolerance of 0.1 is about
                // two standard deviations of a binomial share near 0.3 among 60 villages
                // (sqrt(0.3 * 0.7 / 60) = 0.06): the seed is fixed, so the test is deterministic,
                // and 0.1 is how far a change may move a share before it counts as a change of
                // the weights' effect.
                if (it.value() >= 60) {
                    QVERIFY2(std::abs(share - expected) <= 0.1, qPrintable(u"%1: %2"_s.arg(it.key(), shares.join(u", "_s))));
                }
            }
            qInfo("%s: %d villages, %s", qPrintable(it.key()), it.value(), qPrintable(shares.join(u", "_s)));
        }
        QCOMPARE(seen.size(), 6);
        for (const QString &kind : {u"cross"_s, u"street"_s, u"t"_s, u"l"_s, u"plaza"_s, u"tree"_s}) {
            QVERIFY2(!plansOf(kind).isEmpty(), qPrintable(kind));
        }
    }

    void streetsAreAsDenseAsTheOtherLayouts()
    {
        // Pieces per village and per 100 blocks of road, by layout; streets take pieces on both
        // sides of their main road like the others (decision E-409).
        QHash<QString, QList<int>> pieces;
        QHash<QString, int> roads;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            if (plan->style != u"qtcraft:swamp"_s) {
                pieces[plan->layout].append(int(plan->pieces.size()));
                roads[plan->layout] += plan->roadLength;
            }
        }
        QHash<QString, double> means;
        QStringList lines;
        for (auto it = pieces.cbegin(); it != pieces.cend(); ++it) {
            int sum = 0;
            for (const int count : it.value()) {
                sum += count;
            }
            means.insert(it.key(), double(sum) / double(it.value().size()));
            lines.append(u"%1 %2 (%3 per 100 blocks)"_s.arg(it.key()).arg(means.value(it.key()), 0, 'f', 1).arg(100.0 * sum / roads.value(it.key()), 0, 'f', 1));
        }
        lines.sort();
        qInfo("pieces per village: %s", qPrintable(lines.join(u", "_s)));
        QVERIFY2(means.value(u"street"_s) >= 0.75 * means.value(u"cross"_s), qPrintable(lines.join(u", "_s)));
    }

    void aStreetHasOneLongAxis()
    {
        const Plans plans = plansOf(u"street"_s);
        QVERIFY(plans.size() >= 10);
        int long_ = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QList<QList<QcVillagePlan::Run>> arms = armsFromCentre(*plan);
            QVERIFY(!arms.isEmpty() && arms.size() <= 2);
            if (arms.size() == 2) {
                QCOMPARE(arms[0].front().dx, -arms[1].front().dx);
                QCOMPARE(arms[0].front().dz, -arms[1].front().dz);
            }
            int length = 0;
            for (const QList<QcVillagePlan::Run> &arm : arms) {
                length += lengthOf(arm);
            }
            long_ += length >= 48 ? 1 : 0;
        }
        // Most streets run at least the radius long through the centre (water or a steep step cuts
        // some of them).
        QVERIFY2(qsizetype(long_) * 4 >= plans.size() * 3, qPrintable(u"%1 of %2 streets long"_s.arg(long_).arg(plans.size())));
    }

    void tAndLHaveThreeAndTwoArms()
    {
        for (const auto &[kind, arms] : {std::pair{u"t"_s, 3}, std::pair{u"l"_s, 2}}) {
            const Plans plans = plansOf(kind);
            QVERIFY(plans.size() >= 5);
            for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
                const QList<QList<QcVillagePlan::Run>> found = armsFromCentre(*plan);
                QVERIFY2(!found.isEmpty() && found.size() <= arms, qPrintable(kind));
                // The arms leave along the axes at right angles or opposite.
                for (const QList<QcVillagePlan::Run> &arm : found) {
                    QVERIFY(arm.front().dx == 0 || arm.front().dz == 0);
                }
            }
        }
    }

    void aPlazaRingIsClosed()
    {
        const Plans plans = plansOf(u"plaza"_s);
        QVERIFY(plans.size() >= 5);
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            // The first road is the ring: four runs, each starting where the one before turned, the
            // last one back to the first cell, all around the centre.
            const QList<QList<QcVillagePlan::Run>> arms = armsOf(*plan);
            const QList<QcVillagePlan::Run> &ring = arms.front();
            QCOMPARE(ring.size(), 4);
            for (qsizetype i = 0; i < ring.size(); ++i) {
                const QcVillagePlan::Run &run = ring[i];
                const QcVillagePlan::Run &next = ring[(i + 1) % ring.size()];
                const int endX = run.x + run.dx * (run.length - 1);
                const int endZ = run.z + run.dz * (run.length - 1);
                const int stepX = i + 1 < ring.size() ? next.dx : run.dx;
                const int stepZ = i + 1 < ring.size() ? next.dz : run.dz;
                QCOMPARE(endX + stepX, next.x);
                QCOMPARE(endZ + stepZ, next.z);
            }
            const QSet<qint64> roads = roadColumns(*plan);
            for (const QcVillagePlan::Run &run : ring) {
                for (int d = 0; d < run.length; ++d) {
                    QVERIFY(roads.contains(key(run.x + run.dx * d, run.z + run.dz * d)));
                }
            }
            const QcBlockPos centre = plan->centre;
            QVERIFY(centre.x > ring[0].x && centre.x < ring[0].x + ring[0].length - 1);
            QVERIFY(centre.z > ring[0].z && centre.z < ring[1].z + ring[1].length);
        }
    }

    void aTreeHasArmsAtAngles()
    {
        const Plans plans = plansOf(u"tree"_s);
        QVERIFY(plans.size() >= 10);
        int bent = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QList<QList<QcVillagePlan::Run>> arms = armsFromCentre(*plan);
            QVERIFY2(!arms.isEmpty() && arms.size() <= 5, qPrintable(u"%1 arms"_s.arg(arms.size())));
            const bool diagonal = std::ranges::any_of(plan->runs, [](const QcVillagePlan::Run &run) { return run.dx != 0 && run.dz != 0; });
            // Over water roads keep to the axes; on land the main arm runs along a diagonal.
            QVERIFY(diagonal == (plan->stiltPost == qcAirBlockId));
            bent += std::ranges::any_of(arms, [](const QList<QcVillagePlan::Run> &arm) { return arm.size() >= 2; }) ? 1 : 0;
        }
        QVERIFY2(qsizetype(bent) * 4 >= plans.size() * 3, qPrintable(u"%1 of %2 trees bent"_s.arg(bent).arg(plans.size())));
    }

    void windingRoadsChangeDirection()
    {
        int winding = 0;
        int turned = 0;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            if (!plan->winding || plan->layout == u"tree"_s) {
                continue;
            }
            ++winding;
            bool turns = false;
            const QList<QList<QcVillagePlan::Run>> arms = armsOf(*plan);
            // A plaza's ring (its first road) turns by right angles.
            for (qsizetype a = plan->layout == u"plaza"_s ? 1 : 0; a < arms.size(); ++a) {
                const QList<QcVillagePlan::Run> &arm = arms[a];
                for (qsizetype i = 1; i < arm.size(); ++i) {
                    // A winding road turns by 45 degrees at a time.
                    QVERIFY(std::abs(arm[i].dx - arm[i - 1].dx) + std::abs(arm[i].dz - arm[i - 1].dz) == 1);
                    turns = true;
                }
            }
            turned += turns ? 1 : 0;
        }
        QVERIFY(winding >= 10);
        QVERIFY2(turned * 5 >= winding * 4, qPrintable(u"%1 of %2 winding villages turn"_s.arg(turned).arg(winding)));
    }

    void piecesOverlapNeitherEachOtherNorRoads()
    {
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            const QSet<qint64> roads = roadColumns(*plan);
            QSet<qint64> covered;
            for (const QcStructurePlacement &piece : plan->pieces) {
                for (int z = 0; z < piece.sizeZ(); ++z) {
                    for (int x = 0; x < piece.sizeX(); ++x) {
                        const qint64 column = key(piece.origin().x + x, piece.origin().z + z);
                        QVERIFY2(!covered.contains(column), qPrintable(u"pieces overlap in %1 (%2)"_s.arg(plan->style, plan->layout)));
                        QVERIFY2(!roads.contains(column), qPrintable(u"%1 on a road (%2)"_s.arg(shortName(piece), plan->layout)));
                        covered.insert(column);
                    }
                }
            }
        }
    }

    void everyPieceIsReachableFromTheCentre()
    {
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            // Every piece (lamps aside) has a road cell reached from the centre within two blocks
            // of its edge: on land one block of grass lies between them, over water none.
            const QSet<qint64> reached = reachedFromCentre(*plan);
            for (const QcStructurePlacement &piece : plan->pieces) {
                QVERIFY2(isLamp(piece) || nearAny(piece, reached), qPrintable(u"%1 at %2 %3 unreachable (%4, %5)"_s.arg(shortName(piece))
                                                                                    .arg(piece.origin().x)
                                                                                    .arg(piece.origin().z)
                                                                                    .arg(plan->style, plan->layout)));
            }
        }
    }

    void largeVillagesAreRare()
    {
        // The watchtower's rule (minRoadLength 200) holds for a small share of the villages.
        QList<int> lengths;
        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(m_plans)) {
            lengths.append(plan->roadLength);
        }
        std::ranges::sort(lengths);
        const auto large = std::ranges::count_if(lengths, [](int length) { return length >= 200; });
        qInfo("road length: median %d, 90 %% %d, largest %d; %d of %d at least 200", lengths[lengths.size() / 2],
              lengths[lengths.size() * 9 / 10], lengths.back(), int(large), int(lengths.size()));
        QVERIFY(large >= 1 && large * 4 <= lengths.size());
    }

    void everyLayoutIsBuiltTheSameInAnyOrder()
    {
        // The first village of each kind on land (and one winding and one over water), its chunks
        // in order and backwards on four threads with a fresh generator.
        Plans checked;
        for (const QString &kind : {u"cross"_s, u"street"_s, u"t"_s, u"l"_s, u"plaza"_s, u"tree"_s}) {
            const auto found = std::ranges::find_if(std::as_const(m_plans), [&kind](const std::shared_ptr<const QcVillagePlan> &plan) {
                return plan->layout == kind && plan->stiltPost == qcAirBlockId;
            });
            QVERIFY2(found != std::as_const(m_plans).end(), qPrintable(kind));
            checked.append(*found);
        }
        const auto winding = std::ranges::find_if(std::as_const(m_plans), [](const std::shared_ptr<const QcVillagePlan> &plan) {
            return plan->winding && plan->layout != u"tree"_s;
        });
        const auto overWater = std::ranges::find_if(std::as_const(m_plans), [](const std::shared_ptr<const QcVillagePlan> &plan) {
            return plan->stiltPost != qcAirBlockId && plan->layout != u"cross"_s;
        });
        QVERIFY(winding != std::as_const(m_plans).end() && overWater != std::as_const(m_plans).end());
        checked.append({*winding, *overWater});

        for (const std::shared_ptr<const QcVillagePlan> &plan : std::as_const(checked)) {
            const QList<QcChunkPos> positions = chunksOf(*plan);
            const Chunks first = generateInOrder(m_generator, positions);
            const QcTerrainGenerator fresh(m_content, seed);
            const Chunks second = generateOnThreads(fresh, positions);
            for (const QcChunkPos &pos : positions) {
                QVERIFY2(sameBlocks(*first.value(pos), *second.value(pos)), qPrintable(plan->layout));
            }
            for (const QcStructurePlacement &piece : plan->pieces) {
                QVERIFY2(pieceIsBuilt(first, piece), qPrintable(shortName(piece)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestVillageLayouts)
#include "test_village_layouts.moc"
