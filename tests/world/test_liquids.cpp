#include "common/content/parser/QcContentParser.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/blocks/QcLiquidFlow.h"

#include <QJsonDocument>
#include <QTest>

#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

// Stone, water (7 blocks, every 5 ticks, infinite), lava (3 blocks, every 30 ticks, turns into
// obsidian or cobble next to water), a washable plant and a thin non-washable post.
std::shared_ptr<QcContentRegistry> makeContent()
{
    auto content = std::make_shared<QcContentRegistry>();
    const char *blocks[] = {
        R"({"name":"stone","textures":{"all":"x"}})",
        R"({"name":"obsidian","textures":{"all":"x"}})",
        R"({"name":"cobble","textures":{"all":"x"}})",
        R"({"name":"water","render":"translucent","solid":false,"liquid":true,"replaceable":true,"drops":[],
            "states":{"level":["0","1","2","3","4","5","6","7"],"falling":[false,true]},
            "flow":{"distance":7,"ticks":5,"infinite":true},"textures":{"all":"x"}})",
        R"({"name":"lava","render":"cutout","solid":false,"liquid":true,"replaceable":true,"drops":[],"light":15,
            "states":{"level":["0","1","2","3"],"falling":[false,true]},
            "flow":{"distance":3,"ticks":30,"reactions":[{"with":"water","source":"obsidian","flowing":"cobble"}]},
            "textures":{"all":"x"}})",
        R"({"name":"plant","shape":"cross","textures":{"all":"x"}})",
        R"({"name":"post","solid":false,"render":"cutout","textures":{"all":"x"}})",
    };

    for (const char *text : blocks) {
        const auto block = QcContentParser::parseBlock(json(text), u"t"_s);
        if (!block) {
            // Builds content for a member initialiser, outside any test case; no case can run without it.
            qFatal("%s", qPrintable(block.error()));
        }
        if (!content->registerBlock(*block)) {
            // Builds content for a member initialiser, outside any test case; no case can run without it.
            qFatal("cannot register");
        }
    }
    return content;
}

} // namespace

class TestLiquids : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<QcContentRegistry> m_content = makeContent();

    QcBlockId id(const QString &state) const
    {
        const std::optional<QcBlockId> found = m_content->stateId(u"t:"_s + state);
        if (!found) {
            QcTestUtil::failCase(QStringLiteral("unknown state %1").arg(state));
            return qcAirBlockId;
        }
        return *found;
    }
    QString name(const QcTestWorld &world, QcBlockPos pos) const { return m_content->stateName(*world.blockAt(pos)); }
    QString water(int level, bool falling = false) const
    {
        return u"t:water[falling=%1,level=%2]"_s.arg(falling ? u"true"_s : u"false"_s).arg(level);
    }

    // A stone floor at y = 0 over -radius..radius.
    void floor(QcTestWorld &world, int radius) const { world.fillBox({-radius, 0, -radius}, {radius, 0, radius}, id(u"stone"_s)); }

    // Runs ticks from..to and returns the most steps one tick took.
    int run(QcLiquidFlow &flow, QcTestWorld &world, quint64 from, quint64 to, int budget = QcLiquidFlow::maxStepsPerTick) const
    {
        int most = 0;
        for (quint64 tick = from; tick <= to; ++tick) {
            const QcLiquidFlow::TickResult result =
                flow.tick(tick, world, [&world](QcBlockPos pos, QcBlockId block) { world.set(pos, block); }, budget);
            most = std::max(most, result.steps);
        }
        return most;
    }

    void place(QcLiquidFlow &flow, QcTestWorld &world, QcBlockPos pos, QcBlockId block, quint64 now) const
    {
        world.set(pos, block);
        flow.schedule(world, pos, now);
    }

private slots:
    void parserChecksFlowStates()
    {
        // Levels must count from "0" to the distance, in order.
        const auto missing = QcContentParser::parseBlock(
            json(R"({"name":"w","liquid":true,"solid":false,"flow":{"distance":3},"states":{"level":["0","1","2"]},"textures":{"all":"x"}})"),
            u"t"_s);
        QVERIFY(!missing);
        QVERIFY(missing.error().contains(u"level"_s));

        const auto shuffled = QcContentParser::parseBlock(
            json(R"({"name":"w","liquid":true,"solid":false,"flow":{"distance":1},"states":{"level":["1","0"]},"textures":{"all":"x"}})"),
            u"t"_s);
        QVERIFY(!shuffled);

        // A flow needs a liquid.
        QVERIFY(!QcContentParser::parseBlock(json(R"({"name":"w","flow":{},"textures":{"all":"x"}})"), u"t"_s));

        const QcBlockDefinition &lava = m_content->block(id(u"lava"_s));
        QCOMPARE(lava.flow.distance, 3);
        QCOMPARE(lava.flow.ticks, 30);
        QCOMPARE(lava.flow.reactions.size(), 1);
        QCOMPARE(lava.flow.reactions[0].source, u"t:obsidian"_s);

        // The name alone is a still source.
        QCOMPARE(m_content->stateName(*m_content->blockId(u"t:water"_s)), water(0));
        QVERIFY(QcLiquidFlow::isSource(*m_content, id(u"water"_s)));
    }

    void waterSpreadsSevenBlocksOverFlatGround()
    {
        QcTestWorld world(*m_content);
        floor(world, 12);

        QcLiquidFlow flow(m_content);
        place(flow, world, {0, 1, 0}, id(u"water"_s), 0);
        run(flow, world, 1, 400);
        QCOMPARE(flow.plannedCount(), qsizetype(0));

        // Every cell gets the level of its walking distance from the source, up to 7.
        for (int x = -10; x <= 10; ++x) {
            for (int z = -10; z <= 10; ++z) {
                const int distance = std::abs(x) + std::abs(z);
                const QString expected = distance == 0 ? water(0) : (distance <= 7 ? water(distance) : u"qtcraft:air"_s);
                QCOMPARE(name(world, {x, 1, z}), expected);
                QCOMPARE(name(world, {x, 2, z}), u"qtcraft:air"_s);
            }
        }
    }

    // Like the vanilla game: within dropSearch (4) blocks the liquid only runs towards the nearest
    // drops (decision E-103).
    void waterRunsTowardsTheNearestDrop()
    {
        QcTestWorld world(*m_content);
        floor(world, 12);
        world.set({3, 0, 0}, qcAirBlockId);

        QcLiquidFlow flow(m_content);
        place(flow, world, {0, 1, 0}, id(u"water"_s), 0);
        run(flow, world, 1, 400);

        // A single stream east into the hole; nothing goes the other ways.
        QCOMPARE(name(world, {1, 1, 0}), water(1));
        QCOMPARE(name(world, {2, 1, 0}), water(2));
        QCOMPARE(name(world, {3, 1, 0}), water(3));
        QCOMPARE(name(world, {3, 0, 0}), water(1, true));
        for (const QcBlockPos dry : {QcBlockPos{-1, 1, 0}, QcBlockPos{0, 1, 1}, QcBlockPos{0, 1, -1}, QcBlockPos{1, 1, 1}}) {
            QCOMPARE(name(world, dry), u"qtcraft:air"_s);
        }
    }

    void equallyNearDropsShareTheFlow_data()
    {
        QTest::addColumn<int>("wallX");
        QTest::addColumn<int>("holeX");
        QTest::addColumn<QList<QcBlockPos>>("wet");
        QTest::addColumn<QList<QcBlockPos>>("dry");

        // Holes three blocks east and west: the water splits east and west.
        QTest::newRow("two holes") << 99 << 3 << QList<QcBlockPos>{{2, 1, 0}, {-2, 1, 0}}
                                   << QList<QcBlockPos>{{0, 1, 1}, {0, 1, -1}};
        // A wall right east of the source: the hole two blocks east is four steps away around it,
        // both ways round equally, so the water goes north and south but not west.
        QTest::newRow("around a wall") << 1 << 2 << QList<QcBlockPos>{{0, 1, 1}, {0, 1, -1}, {2, 0, 0}}
                                       << QList<QcBlockPos>{{-1, 1, 0}};
        // The nearest hole is six blocks away, beyond the search: the water spreads evenly.
        QTest::newRow("too far") << 99 << 6 << QList<QcBlockPos>{{0, 1, 1}, {0, 1, -1}, {-1, 1, 0}, {1, 1, 0}}
                                 << QList<QcBlockPos>{};
    }

    void equallyNearDropsShareTheFlow()
    {
        QFETCH(int, wallX);
        QFETCH(int, holeX);
        QFETCH(QList<QcBlockPos>, wet);
        QFETCH(QList<QcBlockPos>, dry);

        QcTestWorld world(*m_content);
        floor(world, 12);
        world.set({holeX, 0, 0}, qcAirBlockId);
        if (wallX == 1) {
            world.set({1, 1, 0}, id(u"stone"_s));
        } else {
            world.set({-holeX, 0, 0}, qcAirBlockId);
        }

        QcLiquidFlow flow(m_content);
        place(flow, world, {0, 1, 0}, id(u"water"_s), 0);
        run(flow, world, 1, 400);
        for (const QcBlockPos &pos : std::as_const(wet)) {
            QVERIFY2(m_content->block(*world.blockAt(pos)).liquid, qPrintable(name(world, pos)));
        }
        for (const QcBlockPos &pos : std::as_const(dry)) {
            QCOMPARE(name(world, pos), u"qtcraft:air"_s);
        }
    }

    void dropSearchIsData()
    {
        QCOMPARE(m_content->block(id(u"water"_s)).flow.dropSearch, 4);
        const auto still = QcContentParser::parseBlock(
            json(R"({"name":"w","liquid":true,"solid":false,"flow":{"distance":1,"dropSearch":0},"states":{"level":["0","1"]},"textures":{"all":"x"}})"),
            u"t"_s);
        QVERIFY2(still.has_value(), qPrintable(still.error()));
        QCOMPARE(still->flow.dropSearch, 0);
        QVERIFY(!QcContentParser::parseBlock(
            json(R"({"name":"w","liquid":true,"solid":false,"flow":{"distance":1,"dropSearch":9},"states":{"level":["0","1"]},"textures":{"all":"x"}})"),
            u"t"_s));

        // Without the search the water spreads evenly despite the hole.
        QcBlockDefinition plainWater = m_content->block(id(u"water"_s));
        plainWater.flow.dropSearch = 0;
        const auto content = makeContent();
        QVERIFY(content->registerBlock(plainWater).has_value());
        QcTestWorld world(*content);
        world.fillBox({-12, 0, -12}, {12, 0, 12}, *content->blockId(u"t:stone"_s));
        world.set({3, 0, 0}, qcAirBlockId);
        QcLiquidFlow flow(content);
        world.set({0, 1, 0}, *content->blockId(u"t:water"_s));
        flow.schedule(world, {0, 1, 0}, 0);
        for (quint64 tick = 1; tick <= 400; ++tick) {
            flow.tick(tick, world, [&world](QcBlockPos pos, QcBlockId block) { world.set(pos, block); });
        }
        QCOMPARE(content->stateName(*world.blockAt({-1, 1, 0})), water(1));
    }

    void waterFallsBeforeItSpreads()
    {
        QcTestWorld world(*m_content);
        floor(world, 12);

        QcLiquidFlow flow(m_content);
        place(flow, world, {0, 5, 0}, id(u"water"_s), 0);
        run(flow, world, 1, 600);

        // The source spreads one level to its sides; everything flowing falls instead of spreading,
        // so five columns reach the floor and spread from there.
        QCOMPARE(name(world, {1, 5, 0}), water(1));
        QCOMPARE(name(world, {2, 5, 0}), u"qtcraft:air"_s);

        const QList<std::pair<int, int>> columns = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &[cx, cz] : columns) {
            for (int y = 1; y <= 4; ++y) {
                QCOMPARE(name(world, {cx, y, cz}), water(1, true));
            }
        }

        for (int x = -10; x <= 10; ++x) {
            for (int z = -10; z <= 10; ++z) {
                int distance = 100;
                for (const auto &[cx, cz] : columns) {
                    distance = std::min(distance, std::abs(x - cx) + std::abs(z - cz));
                }
                if (distance == 0) {
                    continue;
                }
                QCOMPARE(name(world, {x, 1, z}), distance <= 7 ? water(distance) : u"qtcraft:air"_s);
            }
        }
    }

    void flowDrainsWhenTheSourceIsGone()
    {
        QcTestWorld world(*m_content);
        floor(world, 12);

        QcLiquidFlow flow(m_content);
        place(flow, world, {0, 3, 0}, id(u"water"_s), 0);
        run(flow, world, 1, 400);
        QCOMPARE(name(world, {3, 1, 0}), water(2));

        // Taking the source: its neighbours notice (the server plans them on the change).
        world.set({0, 3, 0}, qcAirBlockId);
        for (const QcBlockFace face : qcAllBlockFaces) {
            flow.schedule(world, qcNeighbour(QcBlockPos{0, 3, 0}, face), 400);
        }

        run(flow, world, 401, 1200);
        for (int x = -10; x <= 10; ++x) {
            for (int y = 1; y <= 4; ++y) {
                for (int z = -10; z <= 10; ++z) {
                    QCOMPARE(name(world, {x, y, z}), u"qtcraft:air"_s);
                }
            }
        }
    }

    void twoSourcesMakeAThirdOverGround()
    {
        QcTestWorld world(*m_content);
        floor(world, 6);
        // A channel one block wide between stone walls.
        world.fillBox({-6, 1, -1}, {6, 1, -1}, id(u"stone"_s));
        world.fillBox({-6, 1, 1}, {6, 1, 1}, id(u"stone"_s));

        QcLiquidFlow flow(m_content);
        place(flow, world, {0, 1, 0}, id(u"water"_s), 0);
        place(flow, world, {2, 1, 0}, id(u"water"_s), 0);
        run(flow, world, 1, 200);
        QCOMPARE(name(world, {1, 1, 0}), water(0));

        // Over air there is no new source: the middle falls.
        QcTestWorld air(*m_content);
        air.set({0, 0, 0}, id(u"stone"_s));
        air.set({2, 0, 0}, id(u"stone"_s));
        QcLiquidFlow open(m_content);
        place(open, air, {0, 1, 0}, id(u"water"_s), 0);
        place(open, air, {2, 1, 0}, id(u"water"_s), 0);
        run(open, air, 1, 40);
        QCOMPARE(name(air, {1, 1, 0}), water(1));
        QCOMPARE(name(air, {1, 0, 0}), water(1, true));
    }

    void lavaFlowsShorterAndSlower()
    {
        QcTestWorld world(*m_content);
        floor(world, 8);

        QcLiquidFlow flow(m_content);
        place(flow, world, {0, 1, 0}, id(u"lava"_s), 0);
        run(flow, world, 1, 29);
        QCOMPARE(name(world, {1, 1, 0}), u"qtcraft:air"_s);
        run(flow, world, 30, 30);
        QCOMPARE(name(world, {1, 1, 0}), u"t:lava[falling=false,level=1]"_s);
        run(flow, world, 31, 400);
        QCOMPARE(name(world, {3, 1, 0}), u"t:lava[falling=false,level=3]"_s);
        QCOMPARE(name(world, {2, 1, 1}), u"t:lava[falling=false,level=3]"_s);
        QCOMPARE(name(world, {4, 1, 0}), u"qtcraft:air"_s);

        // Water reaches the same first cell after 5 ticks.
        QcTestWorld wet(*m_content);
        floor(wet, 8);
        QcLiquidFlow waterFlow(m_content);
        place(waterFlow, wet, {0, 1, 0}, id(u"water"_s), 0);
        run(waterFlow, wet, 1, 5);
        QCOMPARE(name(wet, {1, 1, 0}), water(1));
    }

    void lavaTurnsIntoStoneNextToWater()
    {
        QcTestWorld world(*m_content);
        floor(world, 4);

        world.set({0, 1, 0}, id(u"lava"_s));
        QCOMPARE(QcLiquidFlow::reaction(world, {0, 1, 0}, id(u"lava"_s)), std::nullopt);
        world.set({0, 2, 0}, id(u"water[level=3]"_s));
        QCOMPARE(QcLiquidFlow::reaction(world, {0, 1, 0}, id(u"lava"_s)), std::optional<QcBlockId>(id(u"obsidian"_s)));
        QCOMPARE(QcLiquidFlow::reaction(world, {0, 1, 0}, id(u"lava[level=2]"_s)), std::optional<QcBlockId>(id(u"cobble"_s)));

        // Water itself has no reaction.
        QCOMPARE(QcLiquidFlow::reaction(world, {0, 2, 0}, id(u"water[level=3]"_s)), std::nullopt);

        // In a run: water flowing towards a lava source turns the source into obsidian, and lava
        // flowing towards the water turns into cobble where they meet.
        QcTestWorld pool(*m_content);
        floor(pool, 10);

        QcLiquidFlow flow(m_content);
        place(flow, pool, {0, 1, 0}, id(u"lava"_s), 0);
        place(flow, pool, {4, 1, 0}, id(u"water"_s), 0);
        run(flow, pool, 1, 400);
        QCOMPARE(name(pool, {0, 1, 0}), u"t:obsidian"_s);
        QVERIFY(flow.plannedCount() == 0);

        for (int x = -6; x <= 6; ++x) {
            for (int z = -6; z <= 6; ++z) {
                QVERIFY(!name(pool, {x, 1, z}).startsWith(u"t:lava"_s));
            }
        }
    }

    void plantsWashAwayPostsHold()
    {
        QcTestWorld world(*m_content);
        floor(world, 6);
        world.set({1, 1, 0}, id(u"plant"_s));
        world.set({-1, 1, 0}, id(u"post"_s));

        QcLiquidFlow flow(m_content);
        QList<std::pair<QcBlockPos, QString>> replaced;
        place(flow, world, {0, 1, 0}, id(u"water"_s), 0);
        for (quint64 tick = 1; tick <= 200; ++tick) {
            flow.tick(tick, world, [&](QcBlockPos pos, QcBlockId block) {
                replaced.append({pos, m_content->stateName(*world.blockAt(pos))});
                world.set(pos, block);
            });
        }

        QCOMPARE(name(world, {1, 1, 0}), water(1));
        QVERIFY(replaced.contains(std::pair<QcBlockPos, QString>({1, 1, 0}, u"t:plant"_s)));
        QCOMPARE(name(world, {-1, 1, 0}), u"t:post"_s);

        // Around the post the flow comes from the sides: one step further.
        QCOMPARE(name(world, {-2, 1, 0}), water(4));
    }

    void stepsPerTickStayWithinTheBudget()
    {
        QcTestWorld world(*m_content);
        floor(world, 40);

        QcLiquidFlow flow(m_content);
        for (int x = -30; x <= 30; x += 6) {
            for (int z = -30; z <= 30; z += 6) {
                place(flow, world, {x, 6, z}, id(u"water"_s), 0);
            }
        }

        const int most = run(flow, world, 1, 3000, 40);
        QCOMPARE(most, 40);
        QCOMPARE(flow.plannedCount(), qsizetype(0));

        // The bounded run ends in the same state as an unbounded one.
        QcTestWorld free(*m_content);
        floor(free, 40);
        QcLiquidFlow fast(m_content);
        for (int x = -30; x <= 30; x += 6) {
            for (int z = -30; z <= 30; z += 6) {
                place(fast, free, {x, 6, z}, id(u"water"_s), 0);
            }
        }

        run(fast, free, 1, 3000);
        for (int x = -36; x <= 36; ++x) {
            for (int z = -36; z <= 36; ++z) {
                QCOMPARE(name(world, {x, 1, z}), name(free, {x, 1, z}));
            }
        }
    }

    void flowWaitsAtUnloadedBorders()
    {
        QcTestWorld world(*m_content);
        floor(world, 12);

        world.loadedRadius = 3;
        QcLiquidFlow flow(m_content);
        place(flow, world, {0, 1, 0}, id(u"water"_s), 0);
        run(flow, world, 1, 400);

        QCOMPARE(name(world, {3, 1, 0}), water(3));
        QCOMPARE(QcLiquidFlow::fedState(world, {3, 1, 0}, id(u"water[level=3]"_s)), std::nullopt);

        // A cell whose feeder is not loaded keeps its level instead of drying up.
        world.loadedRadius = 12;
        world.set({0, 1, 0}, qcAirBlockId);
        world.set({1, 1, 0}, qcAirBlockId);
        world.set({2, 1, 1}, id(u"stone"_s));
        world.set({2, 1, -1}, id(u"stone"_s));
        world.loadedRadius = 1;
        QCOMPARE(QcLiquidFlow::fedState(world, {1, 1, 0}, id(u"water[level=1]"_s)), std::nullopt);

        // Loading the rest and planning the border cells (the server re-checks borders on load)
        // lets the flow go on to its full distance.
        QcTestWorld again(*m_content);
        floor(again, 12);
        again.loadedRadius = 3;
        QcLiquidFlow resumed(m_content);
        place(resumed, again, {0, 1, 0}, id(u"water"_s), 0);
        run(resumed, again, 1, 400);

        again.loadedRadius = 12;
        for (int z = -3; z <= 3; ++z) {
            for (const int x : {-3, 3}) {
                resumed.schedule(again, {x, 1, z}, 400);
                resumed.schedule(again, {z, 1, x}, 400);
            }
        }

        run(resumed, again, 401, 800);
        QCOMPARE(name(again, {7, 1, 0}), water(7));
        QCOMPARE(name(again, {4, 1, 3}), water(7));
        QCOMPARE(name(again, {8, 1, 0}), u"qtcraft:air"_s);
    }
};

QTEST_GUILESS_MAIN(TestLiquids)
#include "test_liquids.moc"
