#include "common/blockrules/QcBlockRules.h"
#include "common/content/loader/QcBaseContent.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/signal/QcDustNetwork.h"
#include "worldgen/structures/QcStateRotation.h"

#include <QElapsedTimer>
#include <QTest>

#include <deque>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The per-block cascade of the reference, as the measure to beat: every dust that changes asks the
// dust it links to to look again, each recomputing its strength from its neighbours alone. Turning
// a wire off makes the strengths count down step by step.
struct Cascade
{
    QcTestWorld &world;
    const QcSignalTable &table;
    qsizetype evaluations = 0;

    void run(const std::vector<QcBlockPos> &seeds)
    {
        const QcSignalPower power(world, table);
        const QcDustNetwork network(power);
        std::deque<QcBlockPos> queue(seeds.begin(), seeds.end());
        while (!queue.empty()) {
            const QcBlockPos pos = queue.front();
            queue.pop_front();
            const QcSignalState &state = power.at(pos);
            if (state.kind != QcSignalKind::Dust) {
                continue;
            }

            ++evaluations;
            int strength = power.dustInput(pos);
            const QList<QcBlockPos> linked = network.links(pos);
            for (const QcBlockPos &other : linked) {
                strength = std::max(strength, power.at(other).power - 1);
            }
            if (std::cmp_not_equal(strength, state.power)) {
                world.set(pos, table.dustState(*world.blockAt(pos), strength, state.sides));
                queue.insert(queue.end(), linked.begin(), linked.end());
            }
        }
    }
};

} // namespace

// The one-pass recalculation of Signal Dust (decision E-335) on a sparse world: strengths fall by
// one per block, wires climb and descend blocks, opaque blocks above cut a climb, weak power never
// feeds dust, connections give dot, line, cross and slopes, the region recomputed stays within reach
// of the change however long the wire, and the result is the same in every rotation, at other
// coordinates and in any visiting order. The scaling against the reference's cascade is measured.
class TestSignalNetwork : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcSignalTable m_table{*m_content};

    QcBlockId state(const QString &text, int turns = 0) const
    {
        const std::optional<QcBlockId> parsed = m_content->stateId(u"qtcraft:"_s + text);
        if (!parsed) {
            QcTestUtil::failCase(QStringLiteral("unknown state %1").arg(text));
            return qcAirBlockId;
        }
        return QcStateRotation::turned(*m_content, *parsed, turns);
    }

    // A stone floor under y = 0 from (-2, -2) to (size, size).
    static void floor(QcTestWorld &world, const QcContentRegistry &content, int size)
    {
        world.loadedRadius = 1 << 20;
        world.fillBox({-2, -1, -2}, {size, -1, size}, *content.blockId(u"qtcraft:stone"_s));
    }

    // Recomputes around every dust of the world, applies the result and returns the work done.
    qsizetype settle(QcTestWorld &world, const std::vector<QcBlockPos> &seeds) const
    {
        const QcSignalPower power(world, m_table);
        const QcDustNetwork::Result result = QcDustNetwork(power).recompute(seeds);
        for (const QcDustNetwork::Change &change : std::as_const(result.changes)) {
            world.set(change.pos, change.state);
        }
        return result.visited;
    }

    int strength(const QcTestWorld &world, QcBlockPos pos) const { return m_table.at(*world.blockAt(pos)).power; }
    QString side(const QcTestWorld &world, QcBlockPos pos, const char *name) const
    {
        return m_content->property(*world.blockAt(pos), QLatin1StringView(name));
    }

    // A straight wire of `length` dust east from (1, 0, 0) and a lever at (0, 0, 0), on or off.
    std::vector<QcBlockPos> wire(QcTestWorld &world, int length, bool on) const
    {
        floor(world, *m_content, length + 2);
        world.set({0, 0, 0}, state(on ? u"lever[attach=floor,powered=true]"_s : u"lever[attach=floor,powered=false]"_s));
        std::vector<QcBlockPos> dust;
        for (int x = 1; x <= length; ++x) {
            world.set({x, 0, 0}, state(u"signal_dust"_s));
            dust.push_back({x, 0, 0});
        }
        return dust;
    }

private slots:
    void lineLosesOnePerBlock()
    {
        QcTestWorld world(*m_content);
        settle(world, wire(world, 20, true));

        for (int x = 1; x <= 20; ++x) {
            QCOMPARE(strength(world, {x, 0, 0}), std::max(0, 16 - x));
        }
        // Middle dust is a line east to west; the end runs on as a line too.
        QCOMPARE(side(world, {5, 0, 0}, "east"), u"side"_s);
        QCOMPARE(side(world, {5, 0, 0}, "west"), u"side"_s);
        QCOMPARE(side(world, {5, 0, 0}, "north"), u"none"_s);
        QCOMPARE(side(world, {20, 0, 0}, "east"), u"side"_s);
    }

    void dustClimbsAndDescendsBlocks()
    {
        QcTestWorld world(*m_content);
        std::vector<QcBlockPos> dust = wire(world, 3, true);
        // A step up onto a block at x = 4 and down again behind it at x = 5.
        world.set({4, 0, 0}, state(u"stone"_s));
        world.set({4, 1, 0}, state(u"signal_dust"_s));
        world.set({5, 0, 0}, state(u"signal_dust"_s));
        dust.insert(dust.end(), {{4, 1, 0}, {5, 0, 0}});
        settle(world, dust);

        QCOMPARE(side(world, {3, 0, 0}, "east"), u"up"_s);
        QCOMPARE(strength(world, {4, 1, 0}), 12);
        QCOMPARE(strength(world, {5, 0, 0}), 11);
        // The upper dust connects down to the dust beside its block on both sides.
        QCOMPARE(side(world, {4, 1, 0}, "west"), u"side"_s);
        QCOMPARE(side(world, {4, 1, 0}, "east"), u"side"_s);
    }

    void opaqueBlockAboveCutsTheClimb()
    {
        QcTestWorld world(*m_content);
        std::vector<QcBlockPos> dust = wire(world, 3, true);
        world.set({4, 0, 0}, state(u"stone"_s));
        world.set({4, 1, 0}, state(u"signal_dust"_s));
        world.set({3, 1, 0}, state(u"stone"_s));
        dust.push_back({4, 1, 0});
        settle(world, dust);

        QCOMPARE(strength(world, {4, 1, 0}), 0);
        QCOMPARE(side(world, {3, 0, 0}, "east"), u"side"_s);

        // Glass is no conductor: it does not cut.
        world.set({3, 1, 0}, state(u"glass"_s));
        settle(world, dust);
        QCOMPARE(strength(world, {4, 1, 0}), 12);
    }

    void loneDotPointsEverywhere()
    {
        QcTestWorld world(*m_content);
        floor(world, *m_content, 4);
        // A torch under a block powers it strongly; the dot on top takes 15 and points at all sides.
        world.set({1, 0, 1}, state(u"signal_torch[attach=floor]"_s));
        world.set({1, 1, 1}, state(u"stone"_s));
        world.set({1, 2, 1}, state(u"signal_dust"_s));
        settle(world, {{1, 2, 1}});
        QCOMPARE(strength(world, {1, 2, 1}), 15);

        const QcSignalPower power(world, m_table);
        for (const QcBlockFace face : QcSignalTable::horizontal) {
            QCOMPARE(power.inputFrom(qcNeighbour({1, 2, 1}, face), QcBlockRules::opposite(face)), 15);
        }
    }

    void weakPowerNeverFeedsDust()
    {
        QcTestWorld world(*m_content);
        std::vector<QcBlockPos> dust = wire(world, 2, true);
        // The wire points into a block; dust behind the block stays dark, a lamp beside it is powered.
        world.set({3, 0, 0}, state(u"stone"_s));
        world.set({4, 0, 0}, state(u"signal_dust"_s));
        world.set({3, 0, 1}, state(u"signal_lamp"_s));
        dust.push_back({4, 0, 0});
        settle(world, dust);

        QCOMPARE(strength(world, {2, 0, 0}), 14);
        QCOMPARE(strength(world, {4, 0, 0}), 0);
        const QcSignalPower power(world, m_table);
        QCOMPARE(power.weakLevel({3, 0, 0}), 14);
        QCOMPARE(power.strongLevel({3, 0, 0}), 0);
        QCOMPARE(power.input({3, 0, 1}), 14);
    }

    void strongPowerFeedsDust()
    {
        QcTestWorld world(*m_content);
        floor(world, *m_content, 4);
        // A lever on the side of a block powers it strongly: dust on its other side takes 15.
        world.set({1, 0, 1}, state(u"stone"_s));
        world.set({1, 0, 0}, state(u"lever[attach=south,powered=true]"_s));
        world.set({1, 0, 2}, state(u"signal_dust"_s));
        settle(world, {{1, 0, 2}});
        QCOMPARE(strength(world, {1, 0, 2}), 15);
    }

    void connectionsFollowNeighbours()
    {
        QcTestWorld world(*m_content);
        floor(world, *m_content, 4);
        world.set({2, 0, 2}, state(u"signal_dust"_s));
        settle(world, {{2, 0, 2}});
        for (const char *name : {"north", "east", "south", "west"}) {
            QCOMPARE(side(world, {2, 0, 2}, name), u"none"_s);
        }

        // Two neighbours at a corner, then a third: a cross without its fourth arm.
        world.set({3, 0, 2}, state(u"signal_dust"_s));
        world.set({2, 0, 1}, state(u"signal_dust"_s));
        settle(world, {{2, 0, 2}, {3, 0, 2}, {2, 0, 1}});
        QCOMPARE(side(world, {2, 0, 2}, "east"), u"side"_s);
        QCOMPARE(side(world, {2, 0, 2}, "north"), u"side"_s);
        QCOMPARE(side(world, {2, 0, 2}, "west"), u"none"_s);
        world.set({1, 0, 2}, state(u"signal_dust"_s));
        settle(world, {{2, 0, 2}, {1, 0, 2}});
        QCOMPARE(side(world, {2, 0, 2}, "west"), u"side"_s);
        QCOMPARE(side(world, {2, 0, 2}, "south"), u"none"_s);
    }

    void longWireCostsLikeAShortOne()
    {
        // Toggling the lever recomputes the dust within reach, not the whole wire.
        QList<qsizetype> work;
        for (const int length : {32, 1024}) {
            QcTestWorld world(*m_content);
            settle(world, wire(world, length, false));
            world.set({0, 0, 0}, state(u"lever[attach=floor,powered=true]"_s));
            work.append(settle(world, {{1, 0, 0}}));
            QCOMPARE(strength(world, {15, 0, 0}), 1);
            QCOMPARE(strength(world, {16, 0, 0}), 0);
        }
        QCOMPARE(work[0], work[1]);
        QVERIFY2(work[1] <= 16, qPrintable(QString::number(work[1])));
    }

    void wireFedFromBothEndsKeepsTheOtherSide()
    {
        // Levers at both ends of 20 dust; switching the left one off recomputes only the dust in its
        // reach, which takes the right lever's strength from the dust just outside that region.
        QcTestWorld world(*m_content);
        std::vector<QcBlockPos> dust = wire(world, 20, true);
        world.set({21, 0, 0}, state(u"lever[attach=floor,powered=true]"_s));
        settle(world, dust);
        QCOMPARE(strength(world, {10, 0, 0}), 6);

        world.set({0, 0, 0}, state(u"lever[attach=floor,powered=false]"_s));
        settle(world, {{1, 0, 0}});
        for (int x = 1; x <= 20; ++x) {
            QCOMPARE(strength(world, {x, 0, 0}), std::max(0, x - 5));
        }
    }

    void sameWireInEveryTurnAndPlace()
    {
        // An L-shaped wire with a climb, built in four turns at two places, ends with the same
        // strengths cell by cell (seeded in a different order each time).
        QList<QList<int>> results;
        for (const int turns : {0, 1, 2, 3}) {
            for (const int shift : {0, 4096}) {
                QcTestWorld world(*m_content);
                const auto place = [&](int x, int y, int z) {
                    QcBlockPos pos{x, y, z};
                    for (int i = 0; i < turns; ++i) {
                        pos = {-pos.z, pos.y, pos.x};
                    }
                    return pos.offset(shift, 0, -shift);
                };
                world.loadedRadius = 1 << 20;
                world.fillBox({shift - 8, -1, -shift - 8}, {shift + 8, -1, -shift + 8}, state(u"stone"_s));
                world.set(place(0, 0, 0), state(u"lever[attach=floor,powered=true]"_s));
                const QList<QcBlockPos> cells{place(1, 0, 0), place(2, 0, 0), place(3, 0, 0), place(3, 0, 1), place(3, 1, 2), place(3, 1, 3)};
                world.set(place(3, 0, 2), state(u"stone"_s));
                world.set(place(3, 0, 3), state(u"stone"_s));
                std::vector<QcBlockPos> seeds;
                for (const QcBlockPos &cell : cells) {
                    world.set(cell, state(u"signal_dust"_s));
                    seeds.insert((turns % 2 == 0) ? seeds.end() : seeds.begin(), cell);
                }
                settle(world, seeds);
                QList<int> strengths;
                for (const QcBlockPos &cell : cells) {
                    strengths.append(strength(world, cell));
                }
                results.append(strengths);
            }
        }

        QCOMPARE(results.constFirst(), (QList<int>{15, 14, 13, 12, 11, 10}));
        for (const QList<int> &result : std::as_const(results)) {
            QCOMPARE(result, results.constFirst());
        }
    }

    void brokenLinksSwitchTheBlockOff()
    {
        // The base game's links are all valid.
        const QcResult<QcBaseContent::Loaded> base = QcBaseContent::load();
        QVERIFY(base.has_value());
        QVERIFY2(!base->warnings.join(u'\n').contains(u"circuits"_s), qPrintable(base->warnings.join(u'\n')));

        QcContentRegistry content;
        QVERIFY(QcBaseContent::loadInto(content));
        const auto copy = [&content](const char *from, const char *name) {
            QcBlockDefinition block = content.block(*content.blockId(QLatin1StringView(from)));
            block.name = QLatin1StringView(name);
            return block;
        };
        QcBlockDefinition noHead = copy("qtcraft:signal_piston", "test:no_head");
        noHead.signal.head = u"qtcraft:stone"_s;
        QcBlockDefinition sameActive = copy("qtcraft:signal_lamp", "test:same_active");
        sameActive.signal.counterpart = u"qtcraft:signal_lamp"_s;
        QcBlockDefinition missing = copy("qtcraft:signal_torch", "test:missing");
        missing.signal.counterpart = u"test:nothing"_s;
        QcBlockDefinition otherStates = copy("qtcraft:signal_torch", "test:other_states");
        otherStates.signal.counterpart = u"qtcraft:signal_lamp_lit"_s;
        otherStates.signal.kind = QcSignalKind::Lamp;
        otherStates.signal.active = false;
        QcBlockDefinition good = copy("qtcraft:sticky_signal_piston", "test:good_piston");
        for (const QcBlockDefinition &block : {noHead, sameActive, missing, otherStates, good}) {
            QVERIFY(content.registerBlock(block));
        }

        const QStringList warnings = content.finish();
        for (const char *name : {"test:no_head", "test:same_active", "test:missing", "test:other_states"}) {
            QCOMPARE(content.block(*content.blockId(QLatin1StringView(name))).signal.kind, QcSignalKind::None);
            QVERIFY2(warnings.join(u'\n').contains(QLatin1StringView(name)), name);
        }
        QCOMPARE(content.block(*content.blockId(u"test:good_piston"_s)).signal.kind, QcSignalKind::Piston);
    }

    void onePassAgainstCascade_data()
    {
        QTest::addColumn<int>("size");
        QTest::addColumn<bool>("grid");
        for (const int length : {15, 64, 256, 1024, 4096}) {
            QTest::addRow("line %d", length) << length << false;
        }
        for (const int side : {8, 16, 32}) {
            QTest::addRow("grid %dx%d", side, side) << side << true;
        }
    }

    // The measurement of decision E-335: a lever switched on and off at the end of a wire or the
    // corner of a dust grid. Work is counted (dust looked at) and timed; only the counts are checked.
    void onePassAgainstCascade()
    {
        QFETCH(int, size);
        QFETCH(bool, grid);
        const auto build = [&](QcTestWorld &world) {
            if (!grid) {
                return wire(world, size, false);
            }
            floor(world, *m_content, size + 2);
            world.set({0, 0, 0}, state(u"lever[attach=floor,powered=false]"_s));
            std::vector<QcBlockPos> dust;
            for (int x = 1; x <= size; ++x) {
                for (int z = 0; z < size; ++z) {
                    world.set({x, 0, z}, state(u"signal_dust"_s));
                    dust.push_back({x, 0, z});
                }
            }
            return dust;
        };

        QcTestWorld onePassWorld(*m_content);
        QcTestWorld cascadeWorld(*m_content);
        settle(onePassWorld, build(onePassWorld));
        settle(cascadeWorld, build(cascadeWorld));

        qsizetype onePass = 0;
        Cascade cascade{cascadeWorld, m_table};
        QElapsedTimer timer;
        qint64 onePassNs = 0;
        qint64 cascadeNs = 0;
        for (const bool on : {true, false, true, false}) {
            const QcBlockId lever = state(on ? u"lever[attach=floor,powered=true]"_s : u"lever[attach=floor,powered=false]"_s);
            onePassWorld.set({0, 0, 0}, lever);
            cascadeWorld.set({0, 0, 0}, lever);
            timer.start();
            onePass += settle(onePassWorld, {{1, 0, 0}});
            onePassNs += timer.nsecsElapsed();
            timer.start();
            cascade.run({{1, 0, 0}});
            cascadeNs += timer.nsecsElapsed();
        }

        // Both reach the same strengths; the one pass never does more work.
        for (int x = 1; x <= std::min(size, 20); ++x) {
            QCOMPARE(strength(onePassWorld, {x, 0, 0}), strength(cascadeWorld, {x, 0, 0}));
        }
        QVERIFY2(onePass <= cascade.evaluations, qPrintable(u"%1 > %2"_s.arg(onePass).arg(cascade.evaluations)));
        qInfo("SIGNAL-SCALING %s size=%d onePass=%lld cascade=%lld onePassUs=%.1f cascadeUs=%.1f", grid ? "grid" : "line", size,
              qint64(onePass), qint64(cascade.evaluations), double(onePassNs) / 1000.0, double(cascadeNs) / 1000.0);
    }
};

QTEST_GUILESS_MAIN(TestSignalNetwork)
#include "test_signal_network.moc"
