#include "common/content/parser/QcContentParser.h"
#include "common/physics/QcPhysics.h"
#include "common/physics/QcPlayerInput.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/pathing/QcPathFinder.h"

#include <QJsonDocument>
#include <QTest>

#include <cstdlib>

using namespace Qt::StringLiterals;

// Path searches on a sparse test world (decision E-400): a stone floor at y 0, feet at y 1, a body
// of a zombie (1.95 m), everything else placed by the test.
class TestMobPaths : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcPathCellTable m_cells{*m_content};
    QcPathFinder m_finder;
    QcPathRules m_rules;

    QcBlockId block(const QString &name) const
    {
        const std::optional<QcBlockId> id = m_content->stateId(u"qtcraft:"_s + name);
        if (!id) {
            QcTestUtil::failCase(QStringLiteral("unknown block %1").arg(name));
            return qcAirBlockId;
        }
        return *id;
    }

    // A stone floor of 61 x 61 cells around the origin.
    std::unique_ptr<QcTestWorld> makeWorld() const
    {
        auto world = std::make_unique<QcTestWorld>(*m_content);
        world->fillBox({-30, 0, -30}, {30, 0, 30}, block(u"stone"_s));
        return world;
    }

    QcPathResult find(const QcTestWorld &world, QcBlockPos start, QcBlockPos goal, int maxNodes = 4096)
    {
        return m_finder.find(world, m_cells, m_rules, QcPathRequest{start, goal, 0, maxNodes, 256});
    }

    // Every step of the path is a move the finder allows: at most one cell sideways, at most one up,
    // at most QcPathRules::maxDrop down, and every cell is a place to stand.
    void verifySteps(const QcTestWorld &world, QcBlockPos start, const QcPathResult &path) const
    {
        QcBlockPos previous = start;
        QcPathDirectCells cells(world, m_cells);
        for (const QcBlockPos cell : path.cells) {
            QVERIFY2(std::abs(cell.x - previous.x) <= 1 && std::abs(cell.z - previous.z) <= 1, "a step of more than one cell");
            QVERIFY2(cell.y - previous.y <= 1 && previous.y - cell.y <= m_rules.maxDrop, "a step too high or a drop too deep");
            QVERIFY2(QcPathTerrain::standAt(cells, cell, m_rules).ok, "a cell nobody can stand in");
            previous = cell;
        }
    }

    static bool contains(const QcPathResult &path, QcBlockPos cell) { return std::ranges::find(path.cells, cell) != path.cells.end(); }

    bool anyOn(const QcTestWorld &world, const QcPathResult &path, QcBlockId block, int dy) const
    {
        return std::ranges::any_of(path.cells, [&](QcBlockPos cell) { return world.blockAt(cell.offset(0, dy, 0)) == block; });
    }

private slots:
    void init()
    {
        m_rules = QcPathRules{};
        m_rules.height = 1.95;
    }

    void straightOverOpenGround()
    {
        auto world = makeWorld();
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(path.started);
        QVERIFY(path.reachesGoal);
        QCOMPARE(path.cells.size(), std::size_t(8));
        QCOMPARE(path.cells.back(), (QcBlockPos{8, 1, 0}));
        // Open ground needs few nodes: the weighted estimate leads straight on.
        QVERIFY2(path.nodes <= 16, qPrintable(QString::number(path.nodes)));
    }

    void aroundAWall()
    {
        auto world = makeWorld();
        world->fillBox({4, 1, -6}, {4, 3, 6}, block(u"stone"_s));
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(path.reachesGoal);
        verifySteps(*world, {0, 1, 0}, path);
        QVERIFY(std::ranges::none_of(path.cells, [](QcBlockPos cell) { return cell.x == 4 && std::abs(cell.z) <= 6; }));
        QVERIFY(path.cells.size() > 12);
    }

    void throughAGapOneCellWide()
    {
        auto world = makeWorld();
        world->fillBox({4, 1, -30}, {4, 3, 30}, block(u"stone"_s));
        world->fillBox({4, 1, 5}, {4, 2, 5}, qcAirBlockId);
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(path.reachesGoal);
        verifySteps(*world, {0, 1, 0}, path);
        QVERIFY(contains(path, {4, 1, 5}));

        // A gap one block high is too low for the body.
        world->set({4, 2, 5}, block(u"stone"_s));
        QVERIFY(!find(*world, {0, 1, 0}, {8, 1, 0}).reachesGoal);
    }

    void upStepsButNotUpAWall()
    {
        auto world = makeWorld();
        // A staircase of full blocks up to a ledge four blocks high.
        for (int step = 1; step <= 4; ++step) {
            world->fillBox({2 + step, 1, -1}, {2 + step, step, 1}, block(u"stone"_s));
        }
        world->fillBox({7, 1, -1}, {10, 4, 1}, block(u"stone"_s));
        const QcPathResult path = find(*world, {0, 1, 0}, {9, 5, 0});
        QVERIFY(path.reachesGoal);
        verifySteps(*world, {0, 1, 0}, path);

        // Without the steps the ledge is a wall of four blocks: out of reach.
        world->fillBox({3, 1, -1}, {6, 4, 1}, qcAirBlockId);
        const QcPathResult blocked = find(*world, {0, 1, 0}, {9, 5, 0});
        QVERIFY(!blocked.reachesGoal);
        verifySteps(*world, {0, 1, 0}, blocked);
    }

    void stepsOntoSlabs()
    {
        auto world = makeWorld();
        // Half a block up onto a slab, then half a block more onto a full block.
        world->fillBox({3, 1, -1}, {3, 1, 1}, block(u"cobblestone_slab"_s));
        world->fillBox({4, 1, -1}, {6, 1, 1}, block(u"stone"_s));
        const QcPathResult path = find(*world, {0, 1, 0}, {5, 2, 0});
        QVERIFY(path.reachesGoal);
        QVERIFY(contains(path, {3, 1, 0}) || contains(path, {3, 1, -1}) || contains(path, {3, 1, 1}));
    }

    void refusesACliffAndWalksAround()
    {
        auto world = makeWorld();
        // A plateau five blocks high with a staircase down on one side.
        world->fillBox({-10, 1, -10}, {10, 5, 10}, block(u"stone"_s));
        for (int step = 1; step <= 4; ++step) {
            world->fillBox({10 + step, 1, 8}, {10 + step, 5 - step, 10}, block(u"stone"_s));
        }
        const QcPathResult path = find(*world, {5, 6, 0}, {16, 1, 0});
        QVERIFY(path.reachesGoal);
        verifySteps(*world, {5, 6, 0}, path);
        QVERIFY(std::ranges::any_of(path.cells, [](QcBlockPos cell) { return cell.z >= 8 && cell.x > 10; }));

        // Without the staircase the goal is out of reach; the path ends on the plateau's edge.
        world->fillBox({11, 1, 8}, {14, 4, 10}, qcAirBlockId);
        const QcPathResult partial = find(*world, {5, 6, 0}, {16, 1, 0});
        QVERIFY(!partial.reachesGoal);
        QVERIFY(!partial.cells.empty());
        QCOMPARE(partial.cells.back().y, 6);
        QCOMPARE(partial.cells.back().x, 10);
    }

    void dropsOfThreeAreFine()
    {
        auto world = makeWorld();
        world->fillBox({-10, 1, -10}, {3, 3, 10}, block(u"stone"_s));
        const QcPathResult path = find(*world, {0, 4, 0}, {8, 1, 0});
        QVERIFY(path.reachesGoal);
        verifySteps(*world, {0, 4, 0}, path);
    }

    void avoidsWaterWhileThereIsADryWay()
    {
        auto world = makeWorld();
        const QcBlockId water = block(u"water"_s);
        world->fillBox({2, 0, -4}, {5, 0, 4}, water);
        const QcPathResult dry = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(dry.reachesGoal);
        QVERIFY(!anyOn(*world, dry, water, 0));

        // With walls on both sides the only way leads through the water.
        world->fillBox({1, 1, -5}, {6, 3, -5}, block(u"stone"_s));
        world->fillBox({1, 1, 5}, {6, 3, 5}, block(u"stone"_s));
        world->fillBox({1, 1, -30}, {1, 3, -5}, block(u"stone"_s));
        world->fillBox({1, 1, 5}, {1, 3, 30}, block(u"stone"_s));
        const QcPathResult wet = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(wet.reachesGoal);
        QVERIFY(anyOn(*world, wet, water, 0));

        // A swimmer takes the short way through the water anyway.
        world->fillBox({1, 1, -30}, {6, 3, 30}, qcAirBlockId);
        m_rules.swims = true;
        QVERIFY(anyOn(*world, find(*world, {0, 1, 0}, {8, 1, 0}), water, 0));
    }

    void neverIntoLavaOrFire()
    {
        auto world = makeWorld();
        const QcBlockId lava = block(u"lava"_s);
        world->fillBox({3, 0, -30}, {4, 0, 30}, lava);
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(!path.reachesGoal);
        QVERIFY(!anyOn(*world, path, lava, 0));

        world->fillBox({3, 0, -30}, {4, 0, 30}, block(u"stone"_s));
        const QcBlockId fire = block(u"fire"_s);
        world->fillBox({3, 1, -6}, {3, 1, 6}, fire);
        const QcPathResult around = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(around.reachesGoal);
        QVERIFY(!anyOn(*world, around, fire, 0));
    }

    void avoidsMagmaFloors()
    {
        auto world = makeWorld();
        const QcBlockId magma = block(u"magma_block"_s);
        world->fillBox({3, 0, -4}, {5, 0, 4}, magma);
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(path.reachesGoal);
        QVERIFY(!anyOn(*world, path, magma, -1));
    }

    void closedDoorsBlockUnlessTheCreatureOpensThem()
    {
        auto world = makeWorld();
        world->fillBox({4, 1, -30}, {4, 3, 30}, block(u"stone"_s));
        world->set({4, 1, 0}, block(u"oak_door[facing=east,half=bottom,open=false]"_s));
        world->set({4, 2, 0}, block(u"oak_door[facing=east,half=top,open=false]"_s));
        QVERIFY(!find(*world, {0, 1, 0}, {8, 1, 0}).reachesGoal);

        // The villagers of stage 37.2 will open doors.
        m_rules.opensDoors = true;
        QVERIFY(contains(find(*world, {0, 1, 0}, {8, 1, 0}), {4, 1, 0}));

        m_rules.opensDoors = false;
        world->set({4, 1, 0}, block(u"oak_door[facing=east,half=bottom,open=true]"_s));
        world->set({4, 2, 0}, block(u"oak_door[facing=east,half=top,open=true]"_s));
        QVERIFY(contains(find(*world, {0, 1, 0}, {8, 1, 0}), {4, 1, 0}));
    }

    void thinAndLowBlocksAreClassifiedByTheirShape()
    {
        using Kind = QcPathCell::Kind;
        const auto kind = [this](const QString &name) { return QcPathCell::kindOf(m_cells.of(block(name))); };
        const auto lift = [this](const QString &name) { return QcPathCell::liftOf(m_cells.of(block(name))); };
        // A ladder's board leaves room for a body, which holds on to it; panes and bars do not, and
        // neither is a floor.
        QCOMPARE(kind(u"ladder[attach=north]"_s), Kind::Ladder);
        QCOMPARE(kind(u"glass_pane[north=true,south=true]"_s), Kind::Wall);
        QCOMPARE(kind(u"glass_pane"_s), Kind::Wall);
        QCOMPARE(kind(u"iron_bars[east=true,west=true]"_s), Kind::Wall);
        // Closed trapdoors lie flat: a low block at the bottom, a floor at the top.
        QCOMPARE(kind(u"oak_trapdoor[half=bottom,open=false]"_s), Kind::Low);
        QCOMPARE(kind(u"oak_trapdoor[half=top,open=false]"_s), Kind::Solid);
        QCOMPARE(kind(u"oak_trapdoor[open=true]"_s), Kind::Open);
        QCOMPARE(kind(u"iron_trapdoor[half=top,open=false]"_s), Kind::Solid);
        // Wooden doors and gates open by hand, iron doors only by a signal.
        QCOMPARE(kind(u"oak_door[half=bottom,open=false]"_s), Kind::ClosedDoor);
        QCOMPARE(kind(u"oak_fence_gate[open=false]"_s), Kind::ClosedDoor);
        QCOMPARE(kind(u"iron_door[half=bottom,open=false]"_s), Kind::Wall);
        QCOMPARE(kind(u"iron_door[half=bottom,open=true]"_s), Kind::Open);
        // Low blocks carry their height in eighths, rounded up.
        QCOMPARE(lift(u"white_wool_carpet"_s), 0.125);
        QCOMPARE(lift(u"snow_layer"_s), 0.125);
        QCOMPARE(lift(u"oak_trapdoor[half=bottom,open=false]"_s), 0.25);
        QCOMPARE(lift(u"cobblestone_slab"_s), 0.5);
        // A cactus is a block like any other: it does not hurt in this game.
        QCOMPARE(m_cells.of(block(u"cactus"_s)), QcPathCell::make(Kind::Solid));
    }

    void aLadderShaftIsNoFloor()
    {
        auto world = makeWorld();
        // A ledge of stone four high with a shaft of one cell down its middle, a ladder on the
        // shaft's north wall: the opening is no floor to walk over, and five blocks down are too deep
        // (a creature may hold on to the ladder's top to cross it).
        world->fillBox({-10, 1, -10}, {10, 4, 10}, block(u"stone"_s));
        world->fillBox({0, -1, 0}, {0, 4, 0}, qcAirBlockId);
        world->fillBox({0, -1, 0}, {0, 4, 0}, block(u"ladder[attach=north]"_s));
        const QcPathResult path = find(*world, {-3, 5, 0}, {3, 5, 0});
        QVERIFY(path.reachesGoal);
        verifySteps(*world, {-3, 5, 0}, path);
        QVERIFY(!contains(path, {0, 5, 0}));
        QVERIFY(std::ranges::none_of(path.cells, [](QcBlockPos cell) { return cell.y < 4; }));
    }

    void upAndDownALadder()
    {
        auto world = makeWorld();
        // A ledge four high, a ladder on its west face from the floor to the top: up it to the
        // ledge and down it again; without the ladder the ledge is out of reach.
        m_rules.climbsLadders = true;
        world->fillBox({1, 1, -30}, {30, 4, 30}, block(u"stone"_s));
        world->fillBox({0, 1, 0}, {0, 4, 0}, block(u"ladder[attach=east]"_s));
        const QcPathResult up = find(*world, {-4, 1, 0}, {4, 5, 0});
        QVERIFY(up.reachesGoal);
        QVERIFY(contains(up, {0, 2, 0}) && contains(up, {0, 4, 0}));
        const QcPathResult down = find(*world, {4, 5, 0}, {-4, 1, 0});
        QVERIFY(down.reachesGoal);
        QVERIFY(contains(down, {0, 4, 0}));

        world->fillBox({0, 1, 0}, {0, 4, 0}, qcAirBlockId);
        QVERIFY(!find(*world, {-4, 1, 0}, {4, 5, 0}).reachesGoal);
    }

    void onlyClimbersGoUpALadder()
    {
        auto world = makeWorld();
        // The ledge of upAndDownALadder: to a creature that does not climb (a zombie) the ladder is
        // air, no place to stand and no way up.
        world->fillBox({1, 1, -30}, {30, 4, 30}, block(u"stone"_s));
        world->fillBox({0, 1, 0}, {0, 4, 0}, block(u"ladder[attach=east]"_s));
        QVERIFY(!m_rules.climbsLadders);
        const QcPathResult up = find(*world, {-4, 1, 0}, {4, 5, 0});
        QVERIFY(!up.reachesGoal);
        QVERIFY(std::ranges::none_of(up.cells, [](QcBlockPos cell) { return cell.y > 1; }));
        QcPathDirectCells cells(*world, m_cells);
        QVERIFY(!QcPathTerrain::standAt(cells, {0, 3, 0}, m_rules).ok);

        // A zombie's own rules, a villager's.
        const QcMobDefinition &zombie = m_content->mobs()[*m_content->mobIndex(u"qtcraft:zombie"_s)];
        const QcMobDefinition &villager = m_content->mobs()[*m_content->mobIndex(u"qtcraft:villager"_s)];
        QVERIFY(!QcPathRules::of(zombie).climbsLadders);
        QVERIFY(QcPathRules::of(villager).climbsLadders);
        m_rules = QcPathRules::of(zombie);
        QVERIFY(!find(*world, {-4, 1, 0}, {4, 5, 0}).reachesGoal);
        m_rules = QcPathRules::of(villager);
        QVERIFY(find(*world, {-4, 1, 0}, {4, 5, 0}).reachesGoal);
    }

    // The path rules follow the abilities in a creature's data, the AI its "ai" or its kind
    // (decision E-423); villagers have the abilities of the villager AI unless their data says
    // otherwise.
    void abilitiesAndAiComeFromData()
    {
        const auto parse = [](const char *document) { return QcContentParser::parseMob(QJsonDocument::fromJson(document).object(), u"m"_s); };
        const auto plain = parse(R"({"name":"x","skeleton":"b","outfit":"o"})");
        QVERIFY2(plain.has_value(), qcErrorText(plain).constData());
        QCOMPARE(plain->ai, QcMobDefinition::Ai::Passive);
        QCOMPARE(plain->abilities, QcMobAbilities{});
        QCOMPARE(parse(R"({"name":"x","skeleton":"b","outfit":"o","hostile":true})")->ai, QcMobDefinition::Ai::Hostile);

        const auto villager = parse(R"({"name":"x","skeleton":"b","outfit":"o","villager":"v"})");
        QVERIFY2(villager.has_value(), qcErrorText(villager).constData());
        QCOMPARE(villager->ai, QcMobDefinition::Ai::Villager);
        const QcPathRules rules = QcPathRules::of(*villager);
        QVERIFY(rules.opensDoors && rules.climbsLadders && !rules.divesIntoWater && !rules.swims);
        const auto diver = parse(R"({"name":"x","skeleton":"b","outfit":"o","villager":"v","abilities":{"divesIntoWater":true,"climbs":"none"}})");
        QVERIFY(QcPathRules::of(*diver).divesIntoWater && !QcPathRules::of(*diver).climbsLadders && QcPathRules::of(*diver).opensDoors);
        const auto swimmer = parse(R"({"name":"x","skeleton":"b","outfit":"o","abilities":{"swims":true,"climbs":"walls"}})");
        QVERIFY(QcPathRules::of(*swimmer).swims && QcPathRules::of(*swimmer).climbsLadders);

        QVERIFY(parse(R"({"name":"x","skeleton":"b","outfit":"o","ai":"robot"})").error().contains(u"'ai'"_s));
        QVERIFY(parse(R"({"name":"x","skeleton":"b","outfit":"o","ai":"villager"})").error().contains(u"villager AI"_s));
        QVERIFY(parse(R"({"name":"x","skeleton":"b","outfit":"o","villager":"v","ai":"passive"})").error().contains(u"villager AI"_s));
        QVERIFY(parse(R"({"name":"x","skeleton":"b","outfit":"o","ai":"hostile"})").error().contains(u"'hostile'"_s));
        QVERIFY(parse(R"({"name":"x","skeleton":"b","outfit":"o","abilities":{"opensDoors":true}})").error().contains(u"doors"_s));
        QVERIFY(parse(R"({"name":"x","skeleton":"b","outfit":"o","abilities":{"climbs":"trees"}})").error().contains(u"'climbs'"_s));

        // The base content's villager names them, its zombie has none.
        QCOMPARE(m_content->mobs()[*m_content->mobIndex(u"qtcraft:villager"_s)].abilities, villager->abilities);
        QCOMPARE(m_content->mobs()[*m_content->mobIndex(u"qtcraft:zombie"_s)].ai, QcMobDefinition::Ai::Hostile);
    }

    void villagersDoNotJumpIntoDeepWater()
    {
        auto world = makeWorld();
        // The tower beside the pool of dropsDeepOnlyIntoDeepWater: a villager keeps off the jump.
        world->fillBox({-10, 1, -10}, {3, 8, 10}, block(u"stone"_s));
        world->fillBox({4, 1, -10}, {10, 2, 10}, block(u"water"_s));
        world->fillBox({11, 1, -10}, {11, 3, 10}, block(u"stone"_s));
        m_rules.divesIntoWater = false;
        const QcPathResult path = find(*world, {0, 9, 0}, {8, 2, 0});
        QVERIFY(!path.reachesGoal);
        QVERIFY(std::ranges::all_of(path.cells, [](QcBlockPos cell) { return cell.y == 9; }));
    }

    void walksPastALadderOnAWall()
    {
        auto world = makeWorld();
        // A corridor one cell wide with a ladder on its north wall: the ladder's cell is on the way.
        world->fillBox({1, 1, -1}, {7, 3, -1}, block(u"stone"_s));
        world->fillBox({1, 1, 1}, {7, 3, 1}, block(u"stone"_s));
        world->fillBox({-30, 1, 2}, {30, 3, 2}, block(u"stone"_s));
        world->fillBox({-30, 1, -2}, {30, 3, -2}, block(u"stone"_s));
        world->fillBox({4, 1, 0}, {4, 3, 0}, block(u"ladder[attach=north]"_s));
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(path.reachesGoal);
        QVERIFY(contains(path, {4, 1, 0}));
    }

    void bodiesStepFromADirtPathThroughADoor()
    {
        // A dirt path (a sixteenth below the top of its cell) up to an open door in a wall: the
        // full step lifts the head into the lintel, the low step of 0.125 still gets a player and
        // a creature from the path onto the stone in the doorway, without a jump.
        auto world = makeWorld();
        world->fillBox({-30, 0, -30}, {-1, 0, 30}, block(u"dirt_path"_s));
        world->fillBox({0, 1, -30}, {0, 3, 30}, block(u"stone"_s));
        world->fillBox({0, 1, 0}, {0, 1, 0}, block(u"oak_door[facing=east,half=bottom,open=true,hinge=left]"_s));
        world->fillBox({0, 2, 0}, {0, 2, 0}, block(u"oak_door[facing=east,half=top,open=true,hinge=left]"_s));
        const double onPath = 1.0 - 1.0 / 16.0;

        QcEntityMotion player;
        player.position = {-2.5, onPath, 0.5};
        QcPlayerInput east;
        east.yaw = 90.0f;
        east.forward = 1.0f;
        for (int tick = 0; tick < 40; ++tick) {
            player.stepPlayer(east, *world, QcPhysics::tickDuration, QcMovement::Walk);
            QVERIFY(player.position.y < 1.2);
        }
        QVERIFY(player.position.x > 1.5);

        // A villager (1.9 high; a body 1.95 high does not fit under the lintel from the path).
        const QcMobDefinition &villager = m_content->mobs()[*m_content->mobIndex(u"qtcraft:villager"_s)];
        QcEntityMotion mob;
        mob.position = {-2.5, onPath, 0.5};
        const QcPhysics::Steering steering{{1.0, 0.0, 0.0}, villager.walkSpeed, false};
        for (int tick = 0; tick < 60; ++tick) {
            mob.step(steering, QcPhysics::BodySize{villager.width, villager.height}, *world, QcPhysics::tickDuration);
            QVERIFY(mob.position.y < 1.2);
        }
        QVERIFY(mob.position.x > 1.5);
    }

    void trapdoorsAndIronDoors()
    {
        auto world = makeWorld();
        world->fillBox({4, 1, -30}, {4, 3, 30}, block(u"stone"_s));
        // A closed bottom trapdoor in the only gap is walked over; a closed top one closes the gap.
        world->fillBox({4, 1, 0}, {4, 2, 0}, qcAirBlockId);
        world->set({4, 1, 0}, block(u"oak_trapdoor[half=bottom,open=false]"_s));
        world->set({4, 3, 0}, qcAirBlockId);
        QVERIFY(contains(find(*world, {0, 1, 0}, {8, 1, 0}), {4, 1, 0}));
        world->set({4, 1, 0}, block(u"oak_trapdoor[half=top,open=false]"_s));
        QVERIFY(!contains(find(*world, {0, 1, 0}, {8, 1, 0}), {4, 1, 0}));

        // A closed iron door stays shut even for a creature that opens doors; an open one lets it through.
        m_rules.opensDoors = true;
        world->set({4, 1, 0}, block(u"iron_door[facing=east,half=bottom,open=false]"_s));
        world->set({4, 2, 0}, block(u"iron_door[facing=east,half=top,open=false]"_s));
        world->set({4, 3, 0}, block(u"stone"_s));
        QVERIFY(!find(*world, {0, 1, 0}, {8, 1, 0}).reachesGoal);
        world->set({4, 1, 0}, block(u"iron_door[facing=east,half=bottom,open=true]"_s));
        world->set({4, 2, 0}, block(u"iron_door[facing=east,half=top,open=true]"_s));
        QVERIFY(contains(find(*world, {0, 1, 0}, {8, 1, 0}), {4, 1, 0}));
    }

    void carpetsAndSnowLayersAreWalkedOn()
    {
        auto world = makeWorld();
        // A carpeted tunnel two blocks high: a body of 1.8 fits above the carpet, one of 1.95 does not.
        world->fillBox({4, 1, -30}, {4, 3, 30}, block(u"stone"_s));
        world->fillBox({4, 1, 0}, {4, 2, 0}, qcAirBlockId);
        world->set({4, 1, 0}, block(u"white_wool_carpet"_s));
        QVERIFY(!find(*world, {0, 1, 0}, {8, 1, 0}).reachesGoal);
        m_rules.height = 1.8;
        QVERIFY(contains(find(*world, {0, 1, 0}, {8, 1, 0}), {4, 1, 0}));

        // A slab instead lifts the body half a block: no room.
        world->set({4, 1, 0}, block(u"cobblestone_slab"_s));
        QVERIFY(!find(*world, {0, 1, 0}, {8, 1, 0}).reachesGoal);

        // Steps of full blocks covered in snow are climbed (a jump of 1.125 blocks).
        auto hill = makeWorld();
        for (int step = 1; step <= 4; ++step) {
            hill->fillBox({2 + step, 1, -1}, {2 + step, step, 1}, block(u"stone"_s));
            hill->fillBox({2 + step, step + 1, -1}, {2 + step, step + 1, 1}, block(u"snow_layer"_s));
        }
        const QcPathResult path = find(*hill, {0, 1, 0}, {6, 5, 0});
        QVERIFY(path.reachesGoal);
        verifySteps(*hill, {0, 1, 0}, path);
    }

    void panesAreWallsAndNoFloor()
    {
        auto world = makeWorld();
        world->fillBox({4, 1, -30}, {4, 1, 30}, block(u"glass_pane[north=true,south=true]"_s));
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(!path.reachesGoal);
        QVERIFY(std::ranges::none_of(path.cells, [](QcBlockPos cell) { return cell.x >= 4; }));
        QcPathDirectCells cells(*world, m_cells);
        QVERIFY(!QcPathTerrain::standAt(cells, {4, 2, 0}, m_rules).ok);
    }

    void cactusIsAWall()
    {
        auto world = makeWorld();
        world->fillBox({4, 1, -30}, {4, 2, 30}, block(u"cactus"_s));
        QVERIFY(!find(*world, {0, 1, 0}, {8, 1, 0}).reachesGoal);
    }

    void keepsAwayFromLava()
    {
        auto world = makeWorld();
        // A channel of lava along the straight way: the path keeps a cell away from it.
        world->fillBox({2, 0, -1}, {6, 0, -1}, block(u"lava"_s));
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(path.reachesGoal);
        QVERIFY(std::ranges::none_of(path.cells, [](QcBlockPos cell) { return cell.x >= 3 && cell.x <= 5 && cell.z == 0; }));
    }

    void dropsDeepOnlyIntoDeepWater()
    {
        auto world = makeWorld();
        // A tower eight blocks high beside a pool: the way down is the jump into the pool.
        world->fillBox({-10, 1, -10}, {3, 8, 10}, block(u"stone"_s));
        world->fillBox({4, 1, -10}, {10, 2, 10}, block(u"water"_s));
        world->fillBox({11, 1, -10}, {11, 3, 10}, block(u"stone"_s));
        const QcPathResult path = find(*world, {0, 9, 0}, {8, 2, 0});
        QVERIFY(path.reachesGoal);
        QCOMPARE(path.cells.front().y, 9);
        QVERIFY(contains(path, {4, 2, 0}) || contains(path, {4, 2, -1}) || contains(path, {4, 2, 1}));

        // Water one block deep does not break such a fall.
        world->fillBox({4, 2, -10}, {10, 2, 10}, qcAirBlockId);
        QVERIFY(!find(*world, {0, 9, 0}, {8, 1, 0}).reachesGoal);
    }

    void fencesAreWalls()
    {
        auto world = makeWorld();
        world->fillBox({4, 1, -30}, {4, 1, 30}, block(u"oak_fence"_s));
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0});
        QVERIFY(!path.reachesGoal);
        QVERIFY(std::ranges::none_of(path.cells, [](QcBlockPos cell) { return cell.x >= 4; }));
    }

    void stopsAtUnloadedCells()
    {
        auto world = makeWorld();
        world->loadedRadius = 12;
        const QcPathResult path = find(*world, {0, 1, 0}, {25, 1, 0});
        QVERIFY(!path.reachesGoal);
        QVERIFY(!path.cells.empty());
        QVERIFY(std::ranges::all_of(path.cells, [](QcBlockPos cell) { return std::abs(cell.x) <= 12 && std::abs(cell.z) <= 12; }));
        QCOMPARE(path.cells.back().x, 12);
    }

    void keepsToTheNodeBudgetAndGivesTheClosestCell()
    {
        auto world = makeWorld();
        // A long wall: the way around is longer than 40 nodes allow.
        world->fillBox({4, 1, -28}, {4, 3, 28}, block(u"stone"_s));
        const QcPathResult path = find(*world, {0, 1, 0}, {8, 1, 0}, 40);
        QCOMPARE(path.nodes, 40);
        QVERIFY(!path.reachesGoal);
        QVERIFY(!path.cells.empty());
        QCOMPARE(path.cells.back().x, 3);

        // The waypoints are cut at the path length; such a path does not count as getting there.
        auto open = makeWorld();
        const QcPathResult cut = m_finder.find(*open, m_cells, m_rules, QcPathRequest{{0, 1, 0}, {20, 1, 0}, 0, 4096, 8});
        QCOMPARE(cut.cells.size(), std::size_t(8));
        QVERIFY(!cut.reachesGoal);
    }

    void startsFromAFallingOrSwimmingBody()
    {
        auto world = makeWorld();
        // Two blocks above the ground (falling): the search starts on the ground.
        const QcPathResult falling = find(*world, {0, 3, 0}, {4, 1, 0});
        QVERIFY(falling.started);
        QVERIFY(falling.reachesGoal);

        // Inside a wall nothing starts.
        world->fillBox({-3, 1, -3}, {3, 9, 3}, block(u"stone"_s));
        QVERIFY(!find(*world, {0, 2, 0}, {8, 1, 0}).started);
    }
};

QTEST_GUILESS_MAIN(TestMobPaths)
#include "test_mob_paths.moc"
