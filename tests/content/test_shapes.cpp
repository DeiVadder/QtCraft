#include "QcContentParser.h"
#include "QcPhysics.h"
#include "QcRaycast.h"
#include "QcTestWorld.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The player-sized collision box the entity tests move.
constexpr QcPhysics::BodySize playerBody{0.6, 1.8};

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

// Order-independent comparison of box lists.
bool sameBoxes(const QList<QcAabb> &actualBoxes, const QList<QcAabb> &expectedBoxes)
{
    QList<QcAabb> actual = actualBoxes;
    QList<QcAabb> expected = expectedBoxes;
    const auto key = [](const QcAabb &box) {
        return std::tuple{box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z};
    };
    std::ranges::sort(actual, {}, key);
    std::ranges::sort(expected, {}, key);
    return actual == expected;
}

QString describe(const QList<QcAabb> &boxes)
{
    QStringList parts;
    for (const QcAabb &box : boxes) {
        parts.append(u"[%1 %2 %3 - %4 %5 %6]"_s.arg(box.min.x).arg(box.min.y).arg(box.min.z).arg(box.max.x).arg(box.max.y).arg(box.max.z));
    }
    return parts.join(u' ');
}

// Registry with stone, the stairs/slab/fence/gate/ladder/torch shapes of the base game written out
// here, and blocks that use them.
struct Fixture
{
    QcContentRegistry content = qcMakeTestContent();
    QcBlockId stone = *content.blockId(u"test:stone"_s);

    Fixture()
    {
        for (const char *shape : {
                 R"({"name":"stairs","rotate":"facing","flip":"half","boxes":[[0,0,0,1,0.5,1],[0,0.5,0,1,1,0.5]]})",
                 R"({"name":"slab","parts":[{"when":{"type":"bottom"},"boxes":[[0,0,0,1,0.5,1]]},
                     {"when":{"type":"top"},"boxes":[[0,0.5,0,1,1,1]]},{"when":{"type":"double"},"boxes":[[0,0,0,1,1,1]]}]})",
                 R"({"name":"fence","parts":[{"boxes":[[0.375,0,0.375,0.625,1,0.625]],"collision":[[0.375,0,0.375,0.625,1.5,0.625]]}]})",
                 R"({"name":"gate","parts":[{"when":{"open":false},"boxes":[[0,0.25,0.4375,1,1,0.5625]],"collision":[[0,0,0.375,1,1.5,0.625]]},
                     {"when":{"open":true},"boxes":[[0,0.25,0,0.125,1,0.5]],"collision":[]}]})",
                 R"({"name":"ladder","rotate":"attach","boxes":[[0,0,0,1,1,0.0625]]})",
                 R"({"name":"torch","boxes":[[0.4375,0,0.4375,0.5625,0.625,0.5625]],"uv":"box"})",
                 R"({"name":"facing_cube","rotate":"facing","boxes":[[0,0,0,1,1,1]]})"}) {
            const QcResult<QcShapeDefinition> parsed = QcContentParser::parseShape(json(shape), u"t"_s);
            if (!parsed) {
                qFatal("%s", qPrintable(parsed.error()));
            }
            QVERIFY(content.registerShape(*parsed).has_value());
        }

        for (const char *block : {
                 R"({"name":"stairs","shape":"stairs","states":{"facing":["north","east","south","west"],"half":["bottom","top"]},"textures":{"all":"x"}})",
                 R"({"name":"slab","shape":"slab","states":{"type":["bottom","top","double"]},"textures":{"all":"x"}})",
                 R"({"name":"fence","shape":"fence","lightOpacity":0,"textures":{"all":"x"}})",
                 R"({"name":"gate","shape":"gate","lightOpacity":0,"states":{"open":[false,true]},"textures":{"all":"x"}})",
                 R"({"name":"ladder","shape":"ladder","render":"cutout","climbable":true,"lightOpacity":0,"states":{"attach":["north","east","south","west"]},"textures":{"all":"x"}})",
                 R"({"name":"torch","shape":"torch","solid":false,"render":"cutout","light":14,"lightOpacity":0,"textures":{"all":"x"}})",
                 R"({"name":"furnace","shape":"facing_cube","states":{"facing":["north","east","south","west"]},"textures":{"all":"side","north":"front"}})"}) {
            const QcResult<QcBlockDefinition> parsed = QcContentParser::parseBlock(json(block), u"t"_s);
            if (!parsed || !content.registerBlock(*parsed)) {
                qFatal("cannot register %s", block);
            }
        }
    }

    QcBlockId state(const QString &text) const { return *content.stateId(text); }
};

constexpr double dt = QcPhysics::tickDuration;

} // namespace

class TestShapes : public QObject
{
    Q_OBJECT

private:
    Fixture m_fixture;

private slots:
    void stateIdsCountEveryCombination() const
    {
        const QcContentRegistry &content = m_fixture.content;
        // air + stone + water, stairs 4 x 2, slab 3, fence 1, gate 2, ladder 4, torch 1, furnace 4.
        QCOMPARE(content.stateCount(), 3 + 8 + 3 + 1 + 2 + 4 + 1 + 4);

        const QcBlockId stairs = *content.blockId(u"t:stairs"_s);
        // The default state is the first value of every property.
        QCOMPARE(content.property(stairs, u"facing"_s), u"north"_s);
        QCOMPARE(content.property(stairs, u"half"_s), u"bottom"_s);
        QCOMPARE(content.stateName(stairs), u"t:stairs[facing=north,half=bottom]"_s);

        const QcBlockId turned = content.withProperty(stairs, u"facing"_s, u"west"_s);
        QVERIFY(turned != stairs);
        QVERIFY(content.isSameBlock(turned, stairs));
        QCOMPARE(content.property(turned, u"half"_s), u"bottom"_s);
        QCOMPARE(content.withProperty(stairs, u"facing"_s, u"up"_s), stairs);

        // Every state name parses back to its id.
        for (qsizetype id = 0; id < content.stateCount(); ++id) {
            QCOMPARE(content.stateId(content.stateName(QcBlockId(id))), std::optional<QcBlockId>(QcBlockId(id)));
        }

        QStringList problems;
        QCOMPARE(content.stateId(u"t:stairs[half=top,colour=red]"_s, &problems), content.withProperty(stairs, u"half"_s, u"top"_s));
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().contains(u"colour=red"_s));
        QCOMPARE(content.stateId(u"t:unknown"_s), std::nullopt);

        // A damaged entry without its closing bracket keeps the block, in its default state when
        // nothing else parses, and is reported.
        problems.clear();
        QCOMPARE(content.stateId(u"t:stairs[half=top"_s, &problems), content.withProperty(stairs, u"half"_s, u"top"_s));
        QCOMPARE(problems.size(), 1);
        problems.clear();
        QCOMPARE(content.stateId(u"t:stairs[a=b"_s, &problems), stairs);
        QCOMPARE(problems.size(), 2);
    }

    void stairsTurnAndFlip()
    {
        const QcContentRegistry &content = m_fixture.content;
        const QcAabb fullSlab{{0, 0, 0}, {1, 0.5, 1}};
        const auto boxes = [&](const char *state) { return content.state(m_fixture.state(QString::fromLatin1(state))).boxes; };
        const QList<QcAabb> north = boxes("t:stairs[facing=north,half=bottom]");
        QVERIFY2(sameBoxes(north, {fullSlab, {{0, 0.5, 0}, {1, 1, 0.5}}}), qPrintable(describe(north)));

        // Facing east, the high back lies on the east half.
        const QList<QcAabb> east = boxes("t:stairs[facing=east,half=bottom]");
        QVERIFY2(sameBoxes(east, {fullSlab, {{0.5, 0.5, 0}, {1, 1, 1}}}), qPrintable(describe(east)));
        const QList<QcAabb> west = boxes("t:stairs[facing=west,half=bottom]");
        QVERIFY2(sameBoxes(west, {fullSlab, {{0, 0.5, 0}, {0.5, 1, 1}}}), qPrintable(describe(west)));

        // Upside down and facing south: the full half on top, the step below on the south half.
        const QList<QcAabb> top = boxes("t:stairs[facing=south,half=top]");
        QVERIFY2(sameBoxes(top, {{{0, 0.5, 0}, {1, 1, 1}}, {{0, 0, 0.5}, {1, 0.5, 1}}}), qPrintable(describe(top)));

        const QcBlockState &bottomStairs = content.state(m_fixture.state(u"t:stairs[facing=north,half=bottom]"_s));
        QVERIFY(!bottomStairs.fullCube);
        QVERIFY(!bottomStairs.opaque);
        QVERIFY(bottomStairs.fullFace[std::size_t(QcBlockFace::Down)]);
        QVERIFY(bottomStairs.fullFace[std::size_t(QcBlockFace::North)]);
        QVERIFY(!bottomStairs.fullFace[std::size_t(QcBlockFace::Up)]);
        QVERIFY(!bottomStairs.fullFace[std::size_t(QcBlockFace::South)]);
    }

    void slabsAndTurnedCubes()
    {
        const QcContentRegistry &content = m_fixture.content;
        const QcBlockState &bottom = content.state(m_fixture.state(u"t:slab[type=bottom]"_s));
        const QcBlockState &top = content.state(m_fixture.state(u"t:slab[type=top]"_s));
        const QcBlockState &both = content.state(m_fixture.state(u"t:slab[type=double]"_s));

        QCOMPARE(bottom.boxes, (QList<QcAabb>{{{0, 0, 0}, {1, 0.5, 1}}}));
        QCOMPARE(top.boxes, (QList<QcAabb>{{{0, 0.5, 0}, {1, 1, 1}}}));
        QVERIFY(top.fullFace[std::size_t(QcBlockFace::Up)] && !top.fullFace[std::size_t(QcBlockFace::Down)]);

        // A double slab is a full opaque cube again.
        QVERIFY(both.fullCube && both.opaque);

        // A furnace turned east shows its front (authored north) on the east face.
        const QcBlockState &furnace = content.state(m_fixture.state(u"t:furnace[facing=east]"_s));
        QVERIFY(furnace.fullCube);
        QCOMPARE(furnace.faceSource[std::size_t(QcBlockFace::East)], QcBlockFace::North);
        QCOMPARE(furnace.faceSource[std::size_t(QcBlockFace::South)], QcBlockFace::East);
        QCOMPARE(furnace.faceSource[std::size_t(QcBlockFace::Up)], QcBlockFace::Up);
        const QcBlockState &south = content.state(m_fixture.state(u"t:furnace[facing=south]"_s));
        QCOMPARE(south.faceSource[std::size_t(QcBlockFace::South)], QcBlockFace::North);

        // Ladders hang on the side their attach property names.
        QCOMPARE(content.state(m_fixture.state(u"t:ladder[attach=east]"_s)).boxes, (QList<QcAabb>{{{0.9375, 0, 0}, {1, 1, 1}}}));
        QCOMPARE(content.state(m_fixture.state(u"t:ladder[attach=south]"_s)).boxes, (QList<QcAabb>{{{0, 0, 0.9375}, {1, 1, 1}}}));

        // A non-solid torch can be pointed at but not collided with.
        const QcBlockState &torch = content.state(*content.blockId(u"t:torch"_s));
        QVERIFY(torch.collision.isEmpty());
        QCOMPARE(torch.targetBoxes().size(), 1);
        QCOMPARE(torch.boxStretch, QList<bool>{true});
    }

    void parserRejectsBadShapes_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::addColumn<QString>("expected");
        QTest::newRow("outside") << QByteArray(R"({"name":"s","boxes":[[0,0,0,1,1.2,1]]})") << u"'boxes'"_s;
        QTest::newRow("inverted") << QByteArray(R"({"name":"s","boxes":[[0.5,0,0,0.2,1,1]]})") << u"'boxes'"_s;
        QTest::newRow("short") << QByteArray(R"({"name":"s","boxes":[[0,0,0,1,1]]})") << u"'boxes'"_s;
        QTest::newRow("collision too tall") << QByteArray(R"({"name":"s","boxes":[[0,0,0,1,1,1]],"collision":[[0,0,0,1,1.6,1]]})")
                                            << u"'collision'"_s;
        QTest::newRow("empty") << QByteArray(R"({"name":"s","collision":[[0,0,0,1,1,1]]})") << u"at least one box"_s;
        QTest::newRow("uv") << QByteArray(R"({"name":"s","boxes":[[0,0,0,1,1,1]],"uv":"sphere"})") << u"'uv'"_s;
        QTest::newRow("when") << QByteArray(R"({"name":"s","parts":[{"when":{"a":1},"boxes":[[0,0,0,1,1,1]]}]})") << u"'when'"_s;
    }

    void parserRejectsBadShapes()
    {
        QFETCH(QByteArray, document);
        QFETCH(QString, expected);
        const auto parsed = QcContentParser::parseShape(QJsonDocument::fromJson(document).object(), u"t"_s);
        QVERIFY(!parsed.has_value());
        QVERIFY2(parsed.error().contains(expected), qPrintable(parsed.error()));
    }

    void parserRejectsBadStates()
    {
        const auto tooMany = QcContentParser::parseBlock(
            json(R"({"name":"x","states":{"a":["1","2","3","4","5","6","7","8"],"b":["1","2","3","4","5","6","7","8"],)"
                 R"("c":["1","2","3","4","5","6","7","8"],"d":["1","2","3"]},"textures":{"all":"x"}})"),
            u"t"_s);
        QVERIFY(!tooMany.has_value());
        QVERIFY2(tooMany.error().contains(u"states per block"_s), qPrintable(tooMany.error()));
        const auto duplicate = QcContentParser::parseBlock(json(R"({"name":"x","states":{"a":["1","1"]},"textures":{"all":"x"}})"), u"t"_s);
        QVERIFY(!duplicate.has_value());
        const auto slot = QcContentParser::parseBlock(
            json(R"({"name":"x","shape":{"boxes":[[0,0,0,1,1,1]],"texture":"lid"},"textures":{"all":"x"}})"), u"t"_s);
        QVERIFY(!slot.has_value());
        QVERIFY2(slot.error().contains(u"lid"_s), qPrintable(slot.error()));
    }

    void bodiesStandOnSlabsAndStopAtFences()
    {
        const QcContentRegistry &content = m_fixture.content;
        QcTestWorld world(content);
        world.fillBox({-4, 63, -4}, {4, 63, 4}, m_fixture.stone);
        world.set({0, 64, 0}, m_fixture.state(u"t:slab[type=bottom]"_s));

        QcEntityMotion motion;
        motion.position = {0.5, 67.0, 0.5};
        for (int i = 0; i < 60; ++i) {
            QcPhysics::stepEntity(motion, QcPhysics::Steering{{}, 0.0, false}, playerBody, world, dt);
        }
        QCOMPARE(motion.position.y, 64.5);
        QVERIFY(motion.onGround);

        // The fence post at x = 2 collides from x = 2.375 and 1.5 m high.
        world.set({2, 64, 0}, *content.blockId(u"t:fence"_s));
        const QcAabb body = QcPhysics::boxAt({1.0, 64.0, 0.5}, 0.6, 1.8);
        QCOMPARE(QcPhysics::moveBox(body, {2.0, 0.0, 0.0}, world).applied.x, 2.375 - 1.3);
        const QcAabb jumping = QcPhysics::boxAt({1.0, 65.4, 0.5}, 0.6, 1.8);
        QVERIFY(QcPhysics::moveBox(jumping, {2.0, 0.0, 0.0}, world).blockedX);
        const QcAabb above = QcPhysics::boxAt({1.0, 65.55, 0.5}, 0.6, 1.8);
        QVERIFY(!QcPhysics::moveBox(above, {2.0, 0.0, 0.0}, world).blockedX);

        // A closed gate blocks along z; an open one lets bodies through although it is still drawn.
        world.set({0, 64, 2}, m_fixture.state(u"t:gate[open=false]"_s));
        const QcAabb walker = QcPhysics::boxAt({0.5, 64.0, 1.0}, 0.6, 1.8);
        QCOMPARE(QcPhysics::moveBox(walker, {0.0, 0.0, 2.0}, world).applied.z, 2.375 - 1.3);
        world.set({0, 64, 2}, m_fixture.state(u"t:gate[open=true]"_s));
        QCOMPARE(QcPhysics::moveBox(walker, {0.0, 0.0, 2.0}, world).applied.z, 2.0);
        QVERIFY(!content.state(m_fixture.state(u"t:gate[open=true]"_s)).boxes.isEmpty());
    }

    void bodiesWalkUpSlabsAndStairsButNotBlocks()
    {
        const QcContentRegistry &content = m_fixture.content;
        QcTestWorld world(content);
        world.fillBox({-4, 63, -8}, {4, 63, 8}, m_fixture.stone);
        // Bottom slab at z = -2, stairs going up behind it, then a full block at z = -5.
        world.set({0, 64, -2}, m_fixture.state(u"t:slab[type=bottom]"_s));
        world.set({0, 64, -3}, m_fixture.state(u"t:stairs[facing=north,half=bottom]"_s));
        world.set({0, 65, -5}, m_fixture.stone);
        world.set({0, 64, -5}, m_fixture.stone);
        world.set({0, 64, -4}, m_fixture.stone);

        const auto walkNorth = [&](QcEntityMotion &motion, int ticks) {
            for (int i = 0; i < ticks; ++i) {
                QcPhysics::stepEntity(motion, QcPhysics::Steering{{0.0, 0.0, -1.0}, QcPhysics::walkSpeed, false}, playerBody, world, dt);
            }
        };

        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        motion.onGround = true;
        // Onto the slab (0.5 m) and both halves of the stairs, then onto the block behind (one step
        // of 0.5 m from the stairs' top), but not up the two-block wall at z = -5.
        walkNorth(motion, 40);
        QCOMPARE(motion.position.y, 65.0);
        QVERIFY(motion.onGround);
        QCOMPARE(motion.position.z, -4.0 + 0.3);

        // From the ground a full block is not a step.
        QcEntityMotion low;
        low.position = {2.5, 64.0, 0.5};
        low.onGround = true;
        world.set({2, 64, -1}, m_fixture.stone);
        for (int i = 0; i < 20; ++i) {
            QcPhysics::stepEntity(low, QcPhysics::Steering{{0.0, 0.0, -1.0}, QcPhysics::walkSpeed, false}, playerBody, world, dt);
        }
        QCOMPARE(low.position.y, 64.0);
        QCOMPARE(low.position.z, 0.3);

        // Players (server and client prediction both run stepPlayer) walk up the same way.
        QcEntityMotion player;
        player.position = {0.5, 64.0, 0.5};
        player.onGround = true;
        QcPlayerInput input;
        input.forward = 1.0f;
        for (int i = 0; i < 40; ++i) {
            QcPhysics::stepPlayer(player, input, world, dt, false);
        }
        QCOMPARE(player.position.y, 65.0);
    }

    void jumpingAgainstAStepKeepsTheJump()
    {
        const QcContentRegistry &content = m_fixture.content;
        QcTestWorld world(content);
        world.fillBox({-4, 63, -8}, {4, 63, 8}, m_fixture.stone);
        world.set({0, 64, -1}, m_fixture.state(u"t:slab[type=bottom]"_s));

        // Standing against the slab (its south face at z = 0, the body reaches 0.3 m north).
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.3};
        motion.onGround = true;
        QcPhysics::stepEntity(motion, QcPhysics::Steering{{0.0, 0.0, -1.0}, QcPhysics::walkSpeed, true}, playerBody, world, dt);
        // The jump: velocity after one tick of gravity, rise by that velocity over the tick.
        const double velocity = QcPhysics::jumpVelocity - QcPhysics::gravity * dt;
        QCOMPARE(motion.velocity.y, velocity);
        QCOMPARE(motion.position.y, 64.0 + velocity * dt);
        QVERIFY(!motion.onGround);

        // Without the jump the same move steps onto the slab.
        QcEntityMotion step;
        step.position = {0.5, 64.0, 0.3};
        step.onGround = true;
        QcPhysics::stepEntity(step, QcPhysics::Steering{{0.0, 0.0, -1.0}, QcPhysics::walkSpeed, false}, playerBody, world, dt);
        QCOMPARE(step.position.y, 64.5);
    }

    void solidRaysHitTheTopOfFences() const
    {
        const QcContentRegistry &content = m_fixture.content;
        QcTestWorld world(content);
        world.set({0, 64, 0}, *content.blockId(u"t:fence"_s));
        // At 65.2 the ray passes through the cell above the fence, where its collision box reaches.
        const std::optional<QcRaycastHit> hit = QcRaycast::cast(world, {0.5, 65.2, -2.0}, {0.0, 0.0, 1.0}, 5.0, QcRaycast::Targets::Solid);
        QVERIFY(hit);
        QCOMPARE(hit->block, (QcBlockPos{0, 64, 0}));
        QCOMPARE(hit->point.z, 0.375);
        QCOMPARE(hit->face, QcBlockFace::North);
        // Above 1.5 m nothing is hit; the visible fence ends at 1 m.
        QCOMPARE(QcRaycast::cast(world, {0.5, 65.6, -2.0}, {0.0, 0.0, 1.0}, 5.0, QcRaycast::Targets::Solid), std::nullopt);
        QCOMPARE(QcRaycast::cast(world, {0.5, 65.2, -2.0}, {0.0, 0.0, 1.0}, 5.0, QcRaycast::Targets::Visible), std::nullopt);
    }

    void shapeBudgetsStopHostileContent()
    {
        // 65 boxes in one shape: refused by the parser and by the registry.
        QJsonArray parts;
        for (int part = 0; part < 5; ++part) {
            QJsonArray boxes;
            for (int box = 0; box < 13; ++box) {
                boxes.append(QJsonArray{0, 0, 0, 1, 1, 1});
            }
            parts.append(QJsonObject{{u"boxes"_s, boxes}});
        }

        const auto parsed = QcContentParser::parseShape(QJsonObject{{u"name"_s, u"s"_s}, {u"parts"_s, parts}}, u"t"_s);
        QVERIFY(!parsed.has_value());
        QVERIFY2(parsed.error().contains(u"at most 64"_s), qPrintable(parsed.error()));

        QcContentRegistry content = qcMakeTestContent();
        QcShapeDefinition big;
        big.name = u"t:big"_s;
        for (int part = 0; part < 5; ++part) {
            QcShapePart shapePart;
            shapePart.boxes = QList<QcAabb>(13, QcAabb{{0, 0, 0}, {1, 1, 1}});
            big.parts.append(shapePart);
        }
        QVERIFY(!content.registerShape(big).has_value());

        // 1024 states of five properties and 16 parts of 4 boxes each, every part switched by its own
        // property value: about a thousand distinct variants per block. Blocks are refused with a
        // clear message once the shared budget is used up, long before 64 such blocks.
        QcShapeDefinition hostile;
        hostile.name = u"t:hostile"_s;
        for (int part = 0; part < 16; ++part) {
            QcShapePart shapePart;
            shapePart.when = {{u"p%1"_s.arg(part % 5), {QString::number(part / 5)}}};
            shapePart.boxes = QList<QcAabb>(4, QcAabb{{0.1 * (part % 8), 0, 0}, {0.1 * (part % 8) + 0.2, 1, 1}});
            hostile.parts.append(shapePart);
        }
        QVERIFY(content.registerShape(hostile).has_value());

        QcBlockDefinition block;
        block.faceTextures.fill(u"t:x"_s);
        block.shapeName = hostile.name;
        for (int property = 0; property < 5; ++property) {
            block.properties.append({u"p%1"_s.arg(property), {u"0"_s, u"1"_s, u"2"_s, u"3"_s}});
        }
        QCOMPARE(QcBlockStateTable::stateCountOf(block), 1024);
        const qsizetype perBlock = QcBlockStateTable::derivedBoxes(block, hostile);
        QVERIFY2(perBlock > 10000, qPrintable(QString::number(perBlock)));

        const qsizetype before = content.stateTable().totalDerivedBoxes();
        int registered = 0;
        QString error;
        for (int i = 0; i < 64; ++i) {
            block.name = u"t:hostile_%1"_s.arg(i);
            const QcResult<QcBlockId> id = content.registerBlock(block);
            if (!id) {
                error = id.error();
                break;
            }
            ++registered;
        }

        QVERIFY2(registered < 64, qPrintable(QString::number(registered)));
        QCOMPARE(registered, int((QcBlockStateTable::maxDerivedBoxes - before) / perBlock));
        QVERIFY2(error.contains(u"boxes"_s) && error.contains(u"t:hostile_"_s), qPrintable(error));
        QVERIFY(content.stateTable().totalDerivedBoxes() <= QcBlockStateTable::maxDerivedBoxes);
    }

    // States that only turn share their data: 1024 states, four variants.
    void turnedStatesShareTheirData()
    {
        QcShapeDefinition turned;
        turned.name = u"t:turned"_s;
        turned.rotate = u"facing"_s;
        QcShapePart post;
        post.boxes = {{{0.25, 0, 0}, {0.75, 1, 0.25}}};
        turned.parts.append(post);
        QcBlockDefinition many;
        many.name = u"t:many"_s;
        many.faceTextures.fill(u"t:x"_s);
        many.shape = turned;
        many.properties = {{u"facing"_s, {u"north"_s, u"east"_s, u"south"_s, u"west"_s}},
                           {u"a"_s, {u"0"_s, u"1"_s, u"2"_s, u"3"_s, u"4"_s, u"5"_s, u"6"_s, u"7"_s}},
                           {u"b"_s, {u"0"_s, u"1"_s, u"2"_s, u"3"_s, u"4"_s, u"5"_s, u"6"_s, u"7"_s}},
                           {u"c"_s, {u"0"_s, u"1"_s, u"2"_s, u"3"_s}}};
        QCOMPARE(QcBlockStateTable::derivedBoxes(many, turned), 4 * 2);

        QcContentRegistry fresh = qcMakeTestContent();
        const QcResult<QcBlockId> first = fresh.registerBlock(many);
        QVERIFY(first.has_value());
        const QcBlockState &a = fresh.state(*first);
        const QcBlockState &b = fresh.state(fresh.withProperty(*first, u"c"_s, u"3"_s));
        QCOMPARE(a.boxes, b.boxes);
        QVERIFY(a.boxes.isSharedWith(b.boxes));
        QCOMPARE(fresh.state(fresh.withProperty(*first, u"facing"_s, u"east"_s)).boxes,
                 (QList<QcAabb>{{{0.75, 0, 0.25}, {1, 1, 0.75}}}));
    }

    void raysHitTheBoxesNotTheCell()
    {
        const QcContentRegistry &content = m_fixture.content;
        QcTestWorld world(content);
        world.set({0, 64, 0}, m_fixture.state(u"t:slab[type=bottom]"_s));
        // Over the slab, through the empty upper half of its cell: no hit.
        QCOMPARE(QcRaycast::cast(world, {0.5, 64.8, -1.0}, {0.0, 0.0, 1.0}, 3.0), std::nullopt);
        // Through the slab's side.
        const std::optional<QcRaycastHit> side = QcRaycast::cast(world, {0.5, 64.3, -1.0}, {0.0, 0.0, 1.0}, 3.0);
        QVERIFY(side);
        QCOMPARE(side->block, (QcBlockPos{0, 64, 0}));
        QCOMPARE(side->face, QcBlockFace::North);
        QCOMPARE(side->distance, 1.0);
        QCOMPARE(side->point, (QcVec3{0.5, 64.3, 0.0}));
        // From above onto its top at 64.5.
        const std::optional<QcRaycastHit> top = QcRaycast::cast(world, {0.5, 66.0, 0.5}, {0.0, -1.0, 0.0}, 3.0);
        QVERIFY(top);
        QCOMPARE(top->face, QcBlockFace::Up);
        QCOMPARE(top->distance, 1.5);
        QCOMPARE(top->point.y, 64.5);

        // Stairs facing north: the back half reaches 65, the front only 64.5.
        world.set({3, 64, 0}, m_fixture.state(u"t:stairs[facing=north,half=bottom]"_s));
        QCOMPARE(QcRaycast::cast(world, {3.5, 67.0, 0.25}, {0.0, -1.0, 0.0}, 5.0)->point.y, 65.0);
        QCOMPARE(QcRaycast::cast(world, {3.5, 67.0, 0.75}, {0.0, -1.0, 0.0}, 5.0)->point.y, 64.5);
        // A ray at 64.75 from the south enters the back box at z = 0.5 through its south face.
        const std::optional<QcRaycastHit> step = QcRaycast::cast(world, {3.5, 64.75, 3.0}, {0.0, 0.0, -1.0}, 5.0);
        QCOMPARE(step->face, QcBlockFace::South);
        QCOMPARE(step->point.z, 0.5);

        // Torches are targets for the hand, but not for collisions (camera, mobs).
        world.set({6, 64, 0}, *content.blockId(u"t:torch"_s));
        QVERIFY(QcRaycast::cast(world, {6.5, 64.3, -1.0}, {0.0, 0.0, 1.0}, 3.0, QcRaycast::Targets::Visible));
        QCOMPARE(QcRaycast::cast(world, {6.5, 64.3, -1.0}, {0.0, 0.0, 1.0}, 3.0, QcRaycast::Targets::Solid), std::nullopt);
        // Next to the thin torch the ray passes.
        QCOMPARE(QcRaycast::cast(world, {6.2, 64.3, -1.0}, {0.0, 0.0, 1.0}, 3.0), std::nullopt);
    }

    void laddersAreClimbed()
    {
        const QcContentRegistry &content = m_fixture.content;
        QcTestWorld world(content);
        world.fillBox({-3, 63, -3}, {3, 63, 3}, m_fixture.stone);
        world.fillBox({-3, 64, -1}, {3, 75, -1}, m_fixture.stone);
        for (int y = 64; y <= 74; ++y) {
            world.set({0, y, 0}, m_fixture.state(u"t:ladder[attach=north]"_s));
        }

        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        // Pushing against the wall behind the ladder climbs at climbSpeed (2.4 m/s).
        for (int i = 0; i < 40; ++i) {
            QcPhysics::stepEntity(motion, QcPhysics::Steering{{0.0, 0.0, -1.0}, 4.3, false}, playerBody, world, dt);
        }
        QVERIFY(motion.climbing);
        // Two seconds of climbing, minus the first ticks spent reaching the wall: well above 3 m.
        QVERIFY2(motion.position.y > 67.0 && motion.position.y < 64.0 + 2.0 * QcPhysics::climbSpeed + 0.01,
                 qPrintable(QString::number(motion.position.y)));

        // Letting go slides down no faster than climbDownSpeed.
        for (int i = 0; i < 10; ++i) {
            QcPhysics::stepEntity(motion, QcPhysics::Steering{{}, 0.0, false}, playerBody, world, dt);
            QVERIFY(motion.velocity.y >= -QcPhysics::climbDownSpeed);
        }

        // Holding (sneaking) stays in place.
        const double held = motion.position.y;
        for (int i = 0; i < 10; ++i) {
            QcPhysics::stepEntity(motion, QcPhysics::Steering{{}, 0.0, false, true}, playerBody, world, dt);
        }
        QCOMPARE(motion.position.y, held);

        // Without a ladder the same body falls freely.
        QcEntityMotion free;
        free.position = {2.5, 70.0, 0.5};
        QcPhysics::stepEntity(free, QcPhysics::Steering{{}, 0.0, false}, playerBody, world, dt);
        QcPhysics::stepEntity(free, QcPhysics::Steering{{}, 0.0, false}, playerBody, world, dt);
        QVERIFY(!free.climbing);
        QVERIFY(free.velocity.y < -QcPhysics::gravity * dt * 1.5);
    }

    void statesAndShapesReachTheClient() const
    {
        const QcContentRegistry &content = m_fixture.content;
        QByteArray data;
        {
            QDataStream out(&data, QIODevice::WriteOnly);
            content.writeTo(out);
        }

        QDataStream in(data);
        const QcResult<QcContentRegistry> copy = QcContentRegistry::readFrom(in);
        QVERIFY(copy.has_value());
        QCOMPARE(copy->stateCount(), content.stateCount());

        for (qsizetype id = 0; id < content.stateCount(); ++id) {
            const QcBlockState &original = content.state(QcBlockId(id));
            const QcBlockState &received = copy->state(QcBlockId(id));
            QCOMPARE(received.boxes, original.boxes);
            QCOMPARE(received.collision, original.collision);
            QCOMPARE(received.faceSource, original.faceSource);
            QCOMPARE(copy->stateName(QcBlockId(id)), content.stateName(QcBlockId(id)));
        }

        QCOMPARE(copy->block(*copy->blockId(u"t:torch"_s)).lightLevel, 14);
        QVERIFY(copy->block(*copy->blockId(u"t:ladder"_s)).climbable);
    }

    void namedShapesUpdateTheirBlocks()
    {
        QcContentRegistry content;
        QcBlockDefinition post;
        post.name = u"t:post"_s;
        post.shapeName = u"t:post_shape"_s;
        post.faceTextures.fill(u"t:x"_s);
        const QcBlockId id = *content.registerBlock(post);

        // Until the shape exists the block is a cube.
        QVERIFY(content.state(id).fullCube);

        QcShapeDefinition shape;
        shape.name = u"t:post_shape"_s;
        shape.parts = {[] {
            QcShapePart part;
            part.boxes = {{{0.25, 0, 0.25}, {0.75, 1, 0.75}}};
            return part;
        }()};

        QVERIFY(content.registerShape(shape).has_value());
        QCOMPARE(content.state(id).boxes, (QList<QcAabb>{{{0.25, 0, 0.25}, {0.75, 1, 0.75}}}));
        QVERIFY(!content.state(id).fullCube);
    }
};

QTEST_GUILESS_MAIN(TestShapes)
#include "test_shapes.moc"
