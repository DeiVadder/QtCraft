#include "common/physics/QcBodyFit.h"
#include "common/physics/QcPhysics.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/pathing/QcMobNavigator.h"
#include "server/pathing/QcPathFinder.h"
#include "server/pathing/QcPathPlanner.h"
#include "server/pathing/QcPathSteps.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

// A stone floor at y 0 (feet at y 1) and a stone wall across it at x 4, four blocks high.
constexpr int wallX = 4;
constexpr int wallTop = 4;

// What a creature walking to a goal along its paths did.
struct Walk
{
    bool arrived = false;
    int ticks = 0;
    qint64 searches = 0;
    QcVec3 at;
    // It went through the cell at x = wallX, z = 0 (where the tests put a door or a gap too narrow for it).
    bool throughAvoided = false;
    // Its last path got to the goal.
    bool planned = false;
};

} // namespace

// Path searches know how wide a creature is (decision E-909): a body fits through an open door or
// trapdoor only with room beside the panel, nobody walks through the leaf of a free-standing open
// door, and a body wider than a block (spiders, golems) needs two cells side by side. The routes
// planned are routes the physics lets the body take: creatures walking them arrive.
class TestMobWidth : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcPathCellTable m_cells{*m_content};
    QcPathFinder m_finder;

    QcBlockId block(const QString &name) const
    {
        const std::optional<QcBlockId> id = m_content->stateId(u"qtcraft:"_s + name);
        if (!id) {
            QcTestUtil::failCase(u"unknown block %1"_s.arg(name));
            return qcAirBlockId;
        }
        return *id;
    }

    const QcMobDefinition &mob(const QString &name) const { return m_content->mobs()[*m_content->mobIndex(u"qtcraft:"_s + name)]; }
    QcPathRules rules(const QString &name) const { return QcPathRules::of(mob(name)); }

    // The floor, and the wall across it unless `walled` is false.
    std::unique_ptr<QcTestWorld> makeWorld(bool walled = true) const
    {
        auto world = std::make_unique<QcTestWorld>(*m_content);
        world->fillBox({-30, 0, -30}, {30, 0, 30}, block(u"stone"_s));
        if (walled) {
            world->fillBox({wallX, 1, -30}, {wallX, wallTop, 30}, block(u"stone"_s));
        }
        return world;
    }

    // A gap in the wall from z `from` to `to`, three blocks high (a golem is 2.7 high).
    static void openGap(QcTestWorld &world, int from, int to) { world.fillBox({wallX, 1, from}, {wallX, 3, to}, qcAirBlockId); }

    // An open oak door in the wall at z `z`, its leaf along x (the way through it runs along x).
    void placeDoor(QcTestWorld &world, int z) const
    {
        openGap(world, z, z);
        world.set({wallX, 1, z}, block(u"oak_door[facing=east,half=bottom,open=true,hinge=left]"_s));
        world.set({wallX, 2, z}, block(u"oak_door[facing=east,half=top,open=true,hinge=left]"_s));
    }

    QcPathResult find(const QcTestWorld &world, QcPathRules rules, QcBlockPos start, QcBlockPos goal)
    {
        return m_finder.find(world, m_cells, rules, QcPathRequest{start, goal, 0, 8192, 256});
    }

    // Whether the path goes through the wall at `z` (not over it).
    static bool through(const QcPathResult &path, int z)
    {
        return std::ranges::any_of(path.cells, [z](QcBlockPos cell) { return cell.x == wallX && cell.z == z && cell.y < wallTop; });
    }

    // Every waypoint is a place the body fits in: at the middle of its columns, standing on its
    // floor, it overlaps no block.
    void verifyFits(const QcTestWorld &world, const QString &name, const QcPathResult &path) const
    {
        const QcPathRules body = rules(name);
        const QcPathDirectCells cells(world, m_cells);
        for (const QcBlockPos cell : path.cells) {
            const QcPathStand stand = QcPathTerrain::standAt(cells, cell, body);
            QVERIFY2(stand.ok, qPrintable(u"%1 cannot stand at %2 %3 %4"_s.arg(name).arg(cell.x).arg(cell.y).arg(cell.z)));
            const QcVec3 point = QcPathTerrain::pointOf(cell, body) + QcVec3{0.0, stand.lift(), 0.0};
            const QcPhysics::BodySize size{mob(name).width, mob(name).height};
            QVERIFY2(!QcBodyFit::overlapsBlocks(world, point, size, QcBodyFit::Unloaded::Blocked),
                     qPrintable(u"%1 does not fit at %2 %3 %4"_s.arg(name).arg(cell.x).arg(cell.y).arg(cell.z)));
        }
    }

    // The creature `name` walks from `start` to `goal` along its paths with the shared physics
    // (path searches, navigator and steps as on the server, without the rest of its mind).
    // It has arrived within `within` of the goal (horizontally).
    Walk walk(const QcTestWorld &world, const QString &name, const QcVec3 &start, const QcVec3 &goal, double within = 0.6) const
    {
        const QcMobDefinition &definition = mob(name);
        const QcPathRules body = rules(name);
        QcPathPlanner planner(*m_content);
        planner.setLimits(8192, 4096, 256);
        std::vector<QcCreature> mobs(1);
        QcCreature &creature = mobs.front();
        creature.id = 1;
        creature.motion.position = start;
        QcMobNavigator::setGoal(creature, QcMobPath::Purpose::Errand, goal, 0);

        Walk walk;
        for (int tick = 0; tick < 20 * 40 && !walk.arrived; ++tick) {
            QcPhysics::Steering steering = QcMobNavigator::steer(creature, definition.walkSpeed, planner, world, body).steering;
            planner.serve(world, mobs, [body](const QcCreature &) { return body; });
            steering.climbsWalls = body.climbsWalls;
            creature.motion.step(steering, QcPhysics::BodySize{definition.width, definition.height}, world, QcPhysics::tickDuration);
            const QcBlockPos feet = QcPhysics::blockContaining(creature.motion.position);
            walk.throughAvoided = walk.throughAvoided || (feet.x == wallX && feet.z == 0);
            walk.arrived = std::hypot(creature.motion.position.x - goal.x, creature.motion.position.z - goal.z) < within;
            walk.ticks = tick + 1;
        }
        walk.searches = planner.searches();
        walk.planned = creature.path.reachesGoal;
        walk.at = creature.motion.position;
        return walk;
    }

    static QString seen(const QString &name, const Walk &walk)
    {
        return u"%1: arrived %2 after %3 ticks, %4 searches, at %5 %6 %7, through the narrow way %8"_s.arg(name)
            .arg(walk.arrived).arg(walk.ticks).arg(walk.searches).arg(walk.at.x).arg(walk.at.y).arg(walk.at.z).arg(walk.throughAvoided);
    }

private slots:
    // Through an open door (13/16 free beside the leaf) only bodies with 5 cm to spare on each side:
    // zombies (0.6) and cave spiders (0.7), not slimes (0.8), cows (0.9) or golems; a wall without
    // another way leaves the wide ones without a path, a wider gap elsewhere is their way.
    void throughAnOpenDoorOnlyWhatFits()
    {
        auto world = makeWorld();
        placeDoor(*world, 0);
        for (const QString &name : {u"zombie"_s, u"cave_spider"_s, u"villager"_s}) {
            const QcPathResult path = find(*world, rules(name), {0, 1, 0}, {8, 1, 0});
            QVERIFY2(path.reachesGoal && through(path, 0), qPrintable(name));
        }
        for (const QString &name : {u"slime"_s, u"cow"_s, u"pig"_s, u"sheep"_s, u"iron_golem"_s}) {
            QVERIFY2(!find(*world, rules(name), {0, 1, 0}, {8, 1, 0}).reachesGoal, qPrintable(name));
        }

        openGap(*world, 8, 9);
        for (const QString &name : {u"slime"_s, u"cow"_s, u"iron_golem"_s}) {
            const QcPathResult path = find(*world, rules(name), {0, 1, 0}, {8, 1, 0});
            QVERIFY2(path.reachesGoal && !through(path, 0), qPrintable(name));
            verifyFits(*world, name, path);
        }
    }

    // An open trapdoor standing in a doorway two high, at the feet or at head height: the same.
    void throughAnOpenTrapdoorOnlyWhatFits()
    {
        for (const int height : {1, 2}) {
            auto world = makeWorld();
            openGap(*world, 0, 0);
            world->set({wallX, 3, 0}, block(u"stone"_s));
            world->set({wallX, height, 0}, block(u"oak_trapdoor[facing=north,half=bottom,open=true]"_s));
            QVERIFY(through(find(*world, rules(u"zombie"_s), {0, 1, 0}, {8, 1, 0}), 0));
            QVERIFY(!find(*world, rules(u"cow"_s), {0, 1, 0}, {8, 1, 0}).reachesGoal);
        }
    }

    // A free-standing open door: paths go along its leaf through its cell, never through the leaf.
    void notThroughTheLeafOfAFreeStandingDoor()
    {
        auto world = makeWorld(false);
        world->set({wallX, 1, 0}, block(u"oak_door[facing=east,half=bottom,open=true,hinge=left]"_s));
        world->set({wallX, 2, 0}, block(u"oak_door[facing=east,half=top,open=true,hinge=left]"_s));
        const QcPathRules zombie = rules(u"zombie"_s);
        const QcPathDirectCells cells(*world, m_cells);

        // The leaf stands along x: walking along z goes through it.
        QVERIFY(QcPathCell::hasPanel(cells.cell({wallX, 1, 0})));
        QVERIFY(cells.gap({wallX, 1, 0}).z && !cells.gap({wallX, 1, 0}).x);
        for (const auto &[start, goal] : {std::pair{QcBlockPos{wallX, 1, -4}, QcBlockPos{wallX, 1, 4}}, std::pair{QcBlockPos{wallX, 1, 4}, QcBlockPos{wallX, 1, -4}}}) {
            const QcPathResult path = find(*world, zombie, start, goal);
            QVERIFY(path.reachesGoal);
            QcBlockPos previous = start;
            for (const QcBlockPos cell : path.cells) {
                QVERIFY2(!QcPathTerrain::crossesPanel(cells, previous, cell, 0.0, zombie),
                         qPrintable(u"through the leaf from %1 %2 to %3 %4"_s.arg(previous.x).arg(previous.z).arg(cell.x).arg(cell.z)));
                previous = cell;
            }
        }

        // Along the leaf the way through the door's cell stays open.
        QVERIFY(through(find(*world, zombie, {0, 1, 0}, {8, 1, 0}), 0));
    }

    // Diagonals stay open where no panel is: across a floor of slabs (whose height shares the
    // panel's bit in the cell) a path goes straight along the diagonal.
    void diagonalsOverSlabs()
    {
        auto world = makeWorld(false);
        world->fillBox({-1, 1, -1}, {6, 1, 6}, block(u"cobblestone_slab"_s));
        const QcPathResult path = find(*world, rules(u"zombie"_s), {0, 1, 0}, {5, 1, 5});
        QVERIFY(path.reachesGoal);
        QCOMPARE(path.cells.size(), std::size_t{5});
    }

    // A body wider than a block needs two cells side by side: spiders and golems do not go through a
    // gap one block wide, cows do; through a gap two wide both do, and fit at every waypoint.
    void wideBodiesNeedTwoCells()
    {
        auto world = makeWorld();
        openGap(*world, 0, 0);
        QVERIFY(through(find(*world, rules(u"cow"_s), {0, 1, 0}, {8, 1, 0}), 0));
        QVERIFY(!find(*world, rules(u"iron_golem"_s), {0, 1, 0}, {8, 1, 0}).reachesGoal);
        QVERIFY(!find(*world, rules(u"sand_golem"_s), {0, 1, 0}, {8, 1, 0}).reachesGoal);
        // A spider climbs over the wall instead.
        const QcPathResult over = find(*world, rules(u"spider"_s), {0, 1, 0}, {8, 1, 0});
        QVERIFY(!through(over, 0));
        verifyFits(*world, u"spider"_s, over);

        openGap(*world, 6, 7);
        for (const QString &name : {u"spider"_s, u"iron_golem"_s}) {
            const QcPathResult path = find(*world, rules(name), {0, 1, 0}, {8, 1, 0});
            // The spider may still climb over the wall, the golem goes through the wide gap.
            QVERIFY2(path.reachesGoal && !through(path, 0) && (name == u"spider"_s || through(path, 6)), qPrintable(name));
            verifyFits(*world, name, path);
        }
    }

    // A wide body along a corridor two cells wide and round a corner in it.
    void wideBodiesAlongACorridorTwoWide()
    {
        auto world = makeWorld(false);
        // An L of two cells wide: along x from 0 to 9 at z 0-1, then along z up to 9 at x 8-9.
        const QcBlockId stone = block(u"stone"_s);
        world->fillBox({-1, 1, -1}, {10, 3, 10}, stone);
        world->fillBox({0, 1, 0}, {9, 3, 1}, qcAirBlockId);
        world->fillBox({8, 1, 0}, {9, 3, 9}, qcAirBlockId);
        for (const QString &name : {u"iron_golem"_s, u"spider"_s}) {
            const QcPathResult path = find(*world, rules(name), {0, 1, 0}, {8, 1, 8});
            QVERIFY2(path.reachesGoal, qPrintable(name));
            verifyFits(*world, name, path);
        }
    }

    // Creatures walking their paths: a cow facing a door it does not fit through takes the wide gap
    // farther along the wall, a golem and a spider facing a gap one block wide too; none gets stuck.
    void wideCreaturesWalkTheOtherWay_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("door");
        QTest::newRow("cow-door") << u"cow"_s << true;
        QTest::newRow("sheep-door") << u"sheep"_s << true;
        QTest::newRow("iron-golem-gap") << u"iron_golem"_s << false;
        QTest::newRow("spider-gap") << u"spider"_s << false;
    }

    void wideCreaturesWalkTheOtherWay()
    {
        QFETCH(QString, name);
        QFETCH(bool, door);
        auto world = makeWorld();
        // The wall is too high to climb for a spider within its way round (a roof over the gaps).
        world->fillBox({wallX, wallTop + 1, -30}, {wallX, 12, 30}, block(u"stone"_s));
        if (door) {
            placeDoor(*world, 0);
        } else {
            openGap(*world, 0, 0);
        }
        openGap(*world, 5, 6);
        const Walk walked = walk(*world, name, {1.5, 1.0, 0.5}, {7.5, 1.0, 0.5});
        QVERIFY2(walked.arrived && !walked.throughAvoided, qPrintable(seen(name, walked)));
        QVERIFY2(walked.searches <= 2, qPrintable(seen(name, walked)));
    }

    // Narrow creatures still go through the door and the narrow gap.
    void narrowCreaturesStillGoThrough()
    {
        for (const bool door : {true, false}) {
            auto world = makeWorld();
            if (door) {
                placeDoor(*world, 0);
            } else {
                openGap(*world, 0, 0);
            }
            openGap(*world, 5, 6);
            const QString name = door ? u"zombie"_s : u"cow"_s;
            const Walk walked = walk(*world, name, {1.5, 1.0, 0.5}, {7.5, 1.0, 0.5});
            QVERIFY2(walked.arrived && walked.throughAvoided, qPrintable(seen(name, walked)));
        }
    }

    // A spider (1.3 wide) climbs a wall on its path: on the way up it keeps to the wall, close
    // enough to hold on (in the middle of its two cells it would not reach it).
    void aWideSpiderClimbsOnItsPath()
    {
        auto world = makeWorld(false);
        world->fillBox({wallX, 1, -1}, {wallX + 3, 3, 2}, block(u"stone"_s));
        const Walk walked = walk(*world, u"spider"_s, {0.5, 1.0, 1.0}, {wallX + 2.0, 4.0, 1.0});
        QVERIFY2(walked.arrived && walked.at.y > 3.9, qPrintable(seen(u"spider"_s, walked)));
    }

    // A spider climbs a shaft two by two blocks wide: walls on both sides of each axis, it touches
    // one of them (between them it would reach neither).
    void aWideSpiderClimbsAShaftTwoWide()
    {
        auto world = makeWorld(false);
        const QcBlockId stone = block(u"stone"_s);
        world->fillBox({0, 1, 0}, {6, 6, 3}, stone);
        world->fillBox({1, 1, 1}, {2, 6, 2}, qcAirBlockId);
        const Walk walked = walk(*world, u"spider"_s, {2.0, 1.0, 2.0}, {5.0, 7.0, 2.0});
        QVERIFY2(walked.arrived && walked.at.y > 6.9, qPrintable(seen(u"spider"_s, walked)));
        QVERIFY2(walked.searches <= 2, qPrintable(seen(u"spider"_s, walked)));
    }

    // A golem sent to the middle of a cell beside a wall (a patrol point, a wander goal): its path
    // gets there with feet cells whose columns hold the goal's cell, and it walks up to the wall,
    // as near to the goal as its body lets it (0.2 beside a wall), on either side of the wall.
    void aWideBodyReachesAGoalBesideAWall()
    {
        for (const bool wallOnHighSide : {true, false}) {
            auto world = makeWorld(false);
            world->fillBox({wallOnHighSide ? 4 : 2, 1, -30}, {wallOnHighSide ? 4 : 2, 4, 30}, block(u"stone"_s));
            const QcVec3 start = wallOnHighSide ? QcVec3{-3.0, 1.0, 1.0} : QcVec3{9.0, 1.0, 1.0};
            const Walk walked = walk(*world, u"iron_golem"_s, start, {3.5, 1.0, 0.5}, 0.3);
            const QString what = seen(u"iron_golem"_s, walked) + (wallOnHighSide ? u" (wall on +x)"_s : u" (wall on -x)"_s);
            QVERIFY2(walked.arrived && walked.planned, qPrintable(what));
            QVERIFY2(walked.searches <= 2, qPrintable(what));
        }
    }

    // A body wider than a block over floors of different heights: a slab or stairs under one of its
    // columns (it stands on the highest), water under one, a fence post beside its way (never in one
    // of its columns: the post's box is 1.5 high). It fits at every waypoint.
    void wideBodiesOverMixedFloors_data()
    {
        QTest::addColumn<QString>("blockName");
        QTest::addColumn<bool>("row");
        QTest::newRow("slab") << u"cobblestone_slab"_s << true;
        QTest::newRow("stairs") << u"oak_stairs[facing=east,half=bottom,shape=straight]"_s << true;
        QTest::newRow("water") << u"water"_s << true;
        QTest::newRow("fence-post") << u"oak_fence"_s << false;
    }

    void wideBodiesOverMixedFloors()
    {
        QFETCH(QString, blockName);
        QFETCH(bool, row);
        for (const QString &name : {u"iron_golem"_s, u"spider"_s}) {
            auto world = makeWorld(false);
            if (row) {
                world->fillBox({wallX, 1, -30}, {wallX, 1, 30}, block(blockName));
            } else {
                world->set({wallX, 1, 0}, block(blockName));
                world->set({wallX, 1, 1}, block(blockName));
            }
            const QcPathResult path = find(*world, rules(name), {0, 1, 0}, {8, 1, 0});
            QVERIFY2(path.reachesGoal, qPrintable(name));
            verifyFits(*world, name, path);
            const Walk walked = walk(*world, name, {0.5, 1.0, 0.5}, {8.5, 1.0, 0.5});
            QVERIFY2(walked.arrived && walked.searches <= 2, qPrintable(seen(name, walked)));
        }
    }

    // The feet cell of a wide body is the corner cell of its columns, and its point their middle.
    void wideBodiesStandOnTheirCornerCell()
    {
        const QcPathRules golem = rules(u"iron_golem"_s);
        const QcPathRules zombie = rules(u"zombie"_s);
        QCOMPARE(golem.footprint(), 2);
        QCOMPARE(zombie.footprint(), 1);
        QCOMPARE(rules(u"cow"_s).footprint(), 1);
        QCOMPARE(QcPathTerrain::nodeOf({5.0, 1.0, -3.0}, golem), (QcBlockPos{4, 1, -4}));
        QCOMPARE(QcPathTerrain::nodeOf({5.2, 1.0, -2.9}, zombie), (QcBlockPos{5, 1, -3}));
        const QcVec3 golemPoint = QcPathTerrain::pointOf({4, 1, -4}, golem);
        const QcVec3 zombiePoint = QcPathTerrain::pointOf({5, 1, -3}, zombie);
        QVERIFY(golemPoint.x == 5.0 && golemPoint.y == 1.0 && golemPoint.z == -3.0);
        QVERIFY(zombiePoint.x == 5.5 && zombiePoint.y == 1.0 && zombiePoint.z == -2.5);
    }
};

QTEST_GUILESS_MAIN(TestMobWidth)
#include "test_mob_width.moc"
