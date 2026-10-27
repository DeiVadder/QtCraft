#include "common/content/parser/QcContentParser.h"
#include "common/diagnostics/QcThreadCpuClock.h"
#include "QcTestUtil.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;

// Inclined tunnels as surface entrances of mine shafts (decision E-895): the kind is drawn per
// mine shaft from `entranceKinds`, the tunnel is walked in the generated blocks from the top level
// (the central room or a dead end) up to the ground in front of its mouth, one block up per step
// and never two, and comes out the same in any order of the chunks. Fixed seeds plus a random one
// per run (decision E-846).
class TestMineshaftInclines : public QObject
{
    Q_OBJECT

private:
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;
    static constexpr int regions = 3;

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // Inclines from a dead end of the top level, counted over the fixed seeds.
    qsizetype m_fromDeadEnds = 0;

    static QcBlockId at(const Chunks &chunks, QcBlockPos pos)
    {
        const std::shared_ptr<QcChunk> chunk = chunks.value(pos.chunk());
        return chunk && pos.isInsideWorldHeight() ? chunk->block(pos.localX(), pos.y, pos.localZ()) : qcAirBlockId;
    }
    const QcBlockDefinition &block(QcBlockId id) const { return m_content->block(id); }
    bool solid(QcBlockId id) const { return block(id).solid && !block(id).liquid; }
    QString where(QcBlockPos pos) const { return u"%1 %2 %3"_s.arg(pos.x).arg(pos.y).arg(pos.z); }

    // The plans of the test regions with an entrance of `kind`.
    static QList<std::shared_ptr<const QcMineshaftPlan>> plansWith(const QcTerrainGenerator &terrain, QcMineshaftPlan::Entrance::Kind kind)
    {
        QList<std::shared_ptr<const QcMineshaftPlan>> found;
        for (int rz = -regions; rz <= regions; ++rz) {
            for (int rx = -regions; rx <= regions; ++rx) {
                std::shared_ptr<const QcMineshaftPlan> plan = terrain.mineshafts().planOf(rx, rz);
                if (plan && plan->entrance && plan->entrance->kind == kind) {
                    found.append(std::move(plan));
                }
            }
        }
        return found;
    }

    // The chunks of the entrance's cells, in reverse order when `reversed`.
    static Chunks generate(const QcWorldGenerator &terrain, const QcMineshaftPlan &plan, bool reversed = false)
    {
        QList<QcChunkPos> positions;
        const auto add = [&positions](QcBlockPos cell) {
            for (int i = 0; i < 9; ++i) {
                if (const QcChunkPos pos = cell.offset((i % 3 - 1) * 2, 0, (i / 3 - 1) * 2).chunk(); !positions.contains(pos)) {
                    positions.append(pos);
                }
            }
        };
        for (const QcBlockPos &cell : plan.entrance->shaft) {
            add(cell);
        }
        add(plan.entrance->top);
        if (reversed) {
            std::ranges::reverse(positions);
        }
        Chunks chunks;
        for (const QcChunkPos &pos : std::as_const(positions)) {
            chunks.insert(pos, terrain.generate(pos));
        }
        return chunks;
    }

    // The node a tunnel leaves from, found back from its mouth along the plan (no blocks): the
    // index into the plan's nodes, or -1.
    static int startNode(const QcMineshaftPlan &plan)
    {
        const QcMineshaftPlan::Entrance &entrance = *plan.entrance;
        QcBlockPos step = entrance.out;
        for (int steps = 0; steps < 200; ++steps) {
            for (qsizetype i = 0; i < plan.nodes.size(); ++i) {
                if (plan.nodes.at(i).contains(step)) {
                    return static_cast<int>(i);
                }
            }
            step = step.offset(entrance.out.x - entrance.top.x, -1, entrance.out.z - entrance.top.z);
        }
        return -1;
    }

    // Free for walking, or part of a support (posts on the side lanes, a beam in the top row).
    bool freeOrSupport(QcBlockId id) const
    {
        return !block(id).solid || block(id).name == u"qtcraft:oak_log"_s || block(id).name == u"qtcraft:oak_planks"_s;
    }

    // All three lanes of a step: a solid floor, the cells four high free or a support.
    void checkLanes(QcBlockPos middle, int dx, int dz, const Chunks &chunks) const
    {
        for (int lateral = -1; lateral <= 1; ++lateral) {
            const QcBlockPos lane = middle.offset(-dz * lateral, 0, dx * lateral);
            QVERIFY2(solid(at(chunks, lane.offset(0, -1, 0))), qPrintable(u"no floor under "_s + where(lane)));
            for (int dy = 0; dy < 4; ++dy) {
                const bool middleLow = lateral == 0 && dy < 3;
                const QcBlockId id = at(chunks, lane.offset(0, dy, 0));
                QVERIFY2(middleLow ? !block(id).solid : freeOrSupport(id), qPrintable(u"blocked at "_s + where(lane.offset(0, dy, 0))));
            }
        }
    }

    // From the edge of its node up the middle lane to the mouth and out onto the ground: every step
    // one block above the one before on a solid floor with three free cells (feet, head and the jump)
    // and the side lanes and top row free or a support, the step out at most one up and two down; a
    // support at the mouth.
    void walkUp(const QcMineshaftPlan &plan, const Chunks &chunks) const
    {
        const QcMineshaftPlan::Entrance &entrance = *plan.entrance;
        const int dx = entrance.top.x - entrance.out.x;
        const int dz = entrance.top.z - entrance.out.z;
        QCOMPARE(std::abs(dx) + std::abs(dz), 1);

        QcBlockPos step = entrance.out;
        int steps = 0;
        const auto inNode = [&plan](QcBlockPos cell) { return std::ranges::any_of(plan.nodes, [cell](const auto &node) { return node.contains(cell); }); };
        while (!inNode(step)) {
            checkLanes(step, dx, dz, chunks);
            if (QTest::currentTestFailed()) {
                return;
            }
            step = step.offset(-dx, -1, -dz);
            QVERIFY2(++steps < 200, "the tunnel never reaches its node");
        }
        // The step below the first one is the floor level of a node of the top level.
        const auto node = std::ranges::find_if(plan.nodes, [step](const auto &n) { return n.contains(step); });
        QCOMPARE(step.y, node->centre.y);
        QCOMPARE(node->level, 0);
        QVERIFY(steps >= 2);

        const QcBlockPos outside{entrance.top.x, entrance.top.y + 1, entrance.top.z};
        QVERIFY2(solid(at(chunks, entrance.top)), qPrintable(where(entrance.top)));
        QVERIFY(!block(at(chunks, outside)).solid && !block(at(chunks, outside.offset(0, 1, 0))).solid);
        QVERIFY2(outside.y - entrance.out.y <= 1 && entrance.out.y - outside.y <= 2, qPrintable(where(outside)));
        const QcBlockPos post = entrance.out.offset(dz, 0, -dx);
        QCOMPARE(block(at(chunks, post)).name, u"qtcraft:oak_log"_s);
    }

    void checkSeed(qint64 seed, bool fixed)
    {
        const QcTerrainGenerator terrain(m_content, seed);
        const QList<std::shared_ptr<const QcMineshaftPlan>> inclines = plansWith(terrain, QcMineshaftPlan::Entrance::Kind::Incline);
        const QList<std::shared_ptr<const QcMineshaftPlan>> ladders = plansWith(terrain, QcMineshaftPlan::Entrance::Kind::Ladder);
        qInfo().noquote() << u"seed %1: %2 inclined entrances, %3 ladder shafts"_s.arg(seed).arg(inclines.size()).arg(ladders.size());
        if (fixed) {
            QVERIFY(!inclines.isEmpty());
            QVERIFY(!ladders.isEmpty());
        }
        // Walked: the first tunnel out of the central room and the first out of a dead end.
        QList<std::shared_ptr<const QcMineshaftPlan>> walked;
        for (const bool room : {true, false}) {
            const auto found = std::ranges::find_if(inclines, [room](const auto &plan) { return (startNode(*plan) == 0) == room; });
            if (found != inclines.end()) {
                walked.append(*found);
            }
        }
        m_fromDeadEnds += std::ranges::count_if(inclines, [](const auto &plan) { return startNode(*plan) > 0; });
        for (const std::shared_ptr<const QcMineshaftPlan> &plan : std::as_const(walked)) {
            QVERIFY(startNode(*plan) >= 0);
            walkUp(*plan, generate(terrain, *plan));
            if (QTest::currentTestFailed()) {
                qWarning().noquote() << u"seed %1, entrance at %2"_s.arg(seed).arg(where(plan->entrance->top));
                return;
            }
        }
    }

    // The plans of the test regions with `ladder` and `incline` weights, with lattice `radius`.
    QList<std::shared_ptr<const QcMineshaftPlan>> plansWeighted(int ladder, int incline, QcMineshaftPlan::Entrance::Kind kind, int radius) const
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        QcTerrainSettings settings = content->worldgen().terrainSettings();
        settings.mineshafts.ladderWeight = ladder;
        settings.mineshafts.inclineWeight = incline;
        settings.mineshafts.radius = radius;
        content->worldgen().setTerrainSettings(settings);
        return plansWith(QcTerrainGenerator(content, 382), kind);
    }

private slots:
    void inclinesLeadUpToTheGround()
    {
        for (const qint64 seed : {qint64{382}, qint64{51}}) {
            checkSeed(seed, true);
            if (QTest::currentTestFailed()) {
                return;
            }
        }
        // Tunnels leave from dead ends of the top level too, not only from the central room.
        QVERIFY(m_fromDeadEnds > 0);
        checkSeed(QcTestUtil::randomSeed(), false);
    }

    // The drawn kind falls back to the other where it finds no place: with a lattice radius of 1 the
    // mine shaft's extent is too small for most tunnels, so with inclines drawn nearly always, shafts
    // with no incline (none with inclines only) get a ladder; with a weight of 0 a kind never comes.
    void kindsFallBack()
    {
        using Kind = QcMineshaftPlan::Entrance::Kind;
        constexpr int small = 1;
        const QList<std::shared_ptr<const QcMineshaftPlan>> inclinesOnly = plansWeighted(0, 1, Kind::Incline, small);
        const QList<std::shared_ptr<const QcMineshaftPlan>> fallbacks = plansWeighted(1, 100, Kind::Ladder, small);
        const bool fellBack = std::ranges::any_of(fallbacks, [&inclinesOnly](const auto &ladder) {
            return std::ranges::none_of(inclinesOnly, [&ladder](const auto &plan) { return plan->regionX == ladder->regionX && plan->regionZ == ladder->regionZ; });
        });
        QVERIFY(fellBack);
        QVERIFY(plansWeighted(0, 1, Kind::Ladder, small).isEmpty());
        QVERIFY(plansWeighted(1, 0, Kind::Incline, small).isEmpty());
    }

    // A plan with an incline in a fresh generator, its chunks in reverse order: the same entrance
    // and the same blocks in its cells.
    void inclinesAreTheSameInAnyOrder()
    {
        constexpr qint64 seed = 382;
        const QcTerrainGenerator first(m_content, seed);
        const QList<std::shared_ptr<const QcMineshaftPlan>> inclines = plansWith(first, QcMineshaftPlan::Entrance::Kind::Incline);
        QVERIFY(!inclines.isEmpty());
        const QcMineshaftPlan &plan = *inclines.constFirst();
        const Chunks forward = generate(first, plan);

        const QcTerrainGenerator second(m_content, seed);
        const std::shared_ptr<const QcMineshaftPlan> again = second.mineshafts().planOf(plan.regionX, plan.regionZ);
        QVERIFY(again && again->entrance);
        QCOMPARE(again->entrance->top, plan.entrance->top);
        QCOMPARE(again->entrance->shaft, plan.entrance->shaft);
        const Chunks backward = generate(second, *again, true);
        for (const QcBlockPos &cell : plan.entrance->shaft) {
            QCOMPARE(at(backward, cell), at(forward, cell));
            QCOMPARE(at(backward, cell.offset(0, -1, 0)), at(forward, cell.offset(0, -1, 0)));
        }
    }

    // `entranceKinds`: weights 0 to 100 of "ladder" and "incline"; without it ladders only.
    void parsesEntranceKinds()
    {
        const auto shafts = [](const QString &kinds) {
            const QString text = uR"({"stone":"rock","water":"h2o","mineshafts":{%1"planks":"p","post":"p","rail":"r","cobweb":"c",
                                    "light":"l","ladder":"l","seal":"s","spawner":"s","mob":"m","cart":"c"}})"_s;
            return QcContentParser::parseTerrainSettings(QJsonDocument::fromJson(text.arg(kinds).toUtf8()).object(), u"b"_s);
        };
        const auto plain = shafts({});
        QVERIFY2(plain.has_value(), qcErrorText(plain).constData());
        QCOMPARE(plain->mineshafts.ladderWeight, 1);
        QCOMPARE(plain->mineshafts.inclineWeight, 0);

        const auto both = shafts(uR"("entranceKinds":{"ladder":2,"incline":3},)"_s);
        QVERIFY2(both.has_value(), qcErrorText(both).constData());
        QCOMPARE(both->mineshafts.ladderWeight, 2);
        QCOMPARE(both->mineshafts.inclineWeight, 3);
        const auto inclinesOnly = shafts(uR"("entranceKinds":{"incline":1},)"_s);
        QVERIFY(inclinesOnly.has_value());
        QCOMPARE(inclinesOnly->mineshafts.ladderWeight, 0);

        for (const QString &bad : {uR"("entranceKinds":{"stairs":1},)"_s, uR"("entranceKinds":{"incline":-1},)"_s,
                                   uR"("entranceKinds":{"incline":101},)"_s, uR"("entranceKinds":{"ladder":0},)"_s, uR"("entranceKinds":3,)"_s}) {
            const auto rejected = shafts(bad);
            QVERIFY2(!rejected.has_value(), qPrintable(bad));
            qInfo().noquote() << qcErrorText(rejected).constData();
        }
        QCOMPARE(m_content->worldgen().terrainSettings().mineshafts.inclineWeight, 1);
    }

    // Debug-relative cost of the inclined entrances (decision E-895): chunks per second of thread
    // time with the base content against ladders only, interleaved on four areas, plans made
    // afresh. Printed, not checked.
    void measureGenerationCost()
    {
        auto laddersOnly = std::make_shared<QcContentRegistry>(*m_content);
        QcTerrainSettings settings = laddersOnly->worldgen().terrainSettings();
        settings.mineshafts.inclineWeight = 0;
        laddersOnly->worldgen().setTerrainSettings(settings);
        const auto seconds = [](const QcTerrainGenerator &terrain, int offsetX, int offsetZ) {
            const qint64 start = QcThreadCpuClock::nowNs();
            for (int i = 0; i < 64; ++i) {
                terrain.generate({offsetX + i % 8, offsetZ + i / 8});
            }
            return static_cast<double>(QcThreadCpuClock::nowNs() - start) / 1e9;
        };
        const QcTerrainGenerator with(m_content, 382);
        const QcTerrainGenerator without(laddersOnly, 382);
        double withTime = 0.0;
        double withoutTime = 0.0;
        for (const auto &[x, z] : {std::pair{0, 0}, {30, 0}, {0, 30}, {-30, -30}}) {
            withoutTime += seconds(without, x, z);
            withTime += seconds(with, x, z);
        }
        qInfo().noquote() << u"chunks/s ladders only %1, with inclines %2 (%3 %)"_s.arg(256.0 / withoutTime, 0, 'f', 1)
            .arg(256.0 / withTime, 0, 'f', 1)
            .arg((withTime / withoutTime - 1.0) * 100.0, 0, 'f', 1);
    }
};

QTEST_GUILESS_MAIN(TestMineshaftInclines)
#include "test_mineshaft_inclines.moc"
