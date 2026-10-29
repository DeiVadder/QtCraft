// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"
#include "QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "server/mobs/QcMobManager.h"
#include "server/pathing/QcPathCells.h"
#include "server/pathing/QcPathPlanner.h"
#include "worldgen/generators/QcFlatGenerator.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = 0.05;
constexpr double midnight = QcDayCycle::dayLength * 0.75;
// The flat world's ground is grass at y 4; feet stand at y 5.
constexpr int ground = 5;
// A door leaf or an open trapdoor is 3/16 block thick: the free opening beside it is 13/16 wide,
// its middle 19/32 from the panel's side (or 13/32 from the other).
constexpr double panel = 3.0 / 16.0;
constexpr double openingMiddle = (panel + 1.0) / 2.0;
// A body 0.6 wide in the middle of the opening keeps (13/16 - 0.6) / 2, about 10.6 cm, to the panel
// and to the door frame; the walks below must keep most of it.
constexpr double minClearance = 0.08;

// What one walk through the doors saw.
struct Walk
{
    bool through = false;
    int ticks = 0;
    // Path searches on the way: one, unless the creature got stuck or was pushed off its path.
    qint64 searches = 0;
    int jumps = 0;
    // How often it went across the line through the first door and back while it came along the
    // cell before that door (more than a hair off the line on both sides).
    int crossings = 0;
    std::optional<int> side;
    // The least space between the body and a panel while the body was beside it along the way.
    double clearance = std::numeric_limits<double>::max();

    // Notes the creature `offset` across the line while it comes along the cell before the door.
    void noteOffset(double offset)
    {
        if (std::abs(offset) < 0.01) {
            return;
        }
        const int now = offset > 0.0 ? 1 : -1;
        crossings += side && *side != now ? 1 : 0;
        side = now;
    }
};

// Doors or trapdoors in walls across the way, which runs along z (`alongZ`) or x; the panels' boxes
// in the world. The way crosses the walls at across 0.
struct Scene
{
    bool alongZ = true;
    std::vector<QcAabb> panels;

    QcBlockPos cell(int along, int across, int dy) const
    {
        return alongZ ? QcBlockPos{across, ground + dy, along} : QcBlockPos{along, ground + dy, across};
    }
    QcVec3 point(double along, double across) const
    {
        return alongZ ? QcVec3{0.5 + across, ground, 0.5 + along} : QcVec3{0.5 + along, ground, 0.5 + across};
    }
    double alongOf(const QcVec3 &position) const { return (alongZ ? position.z : position.x) - 0.5; }
    double acrossOf(const QcVec3 &position) const { return alongZ ? position.x : position.z; }
    // The middle of the opening beside the first panel, across the way (the cell at across 0).
    double line() const
    {
        const double low = alongZ ? panels.front().min.x : panels.front().min.z;
        const double high = alongZ ? panels.front().max.x : panels.front().max.z;
        return low <= 0.0 ? (high + 1.0) / 2.0 : low / 2.0;
    }
};

} // namespace

// Creatures going through open doors and trapdoors steer to the middle of the free opening beside
// the panel, not to the middle of the cell (decision E-905): from every facing, hinge side and
// direction, coming in off the line and steeply from the side, through double doors, two doors in
// a row and trapdoors at the feet or at head height, they keep clear of the panels and never jump.
class TestDoorSteering : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    // The creature that walks (a mob name of the base content) and whether it is shoved on its way.
    QString m_walker = u"zombie"_s;
    bool m_push = false;

    QcBlockId state(const QString &text) const
    {
        const std::optional<QcBlockId> id = m_content->stateId(text);
        if (!id) {
            qFatal("unknown state %s", qPrintable(text));
        }
        return *id;
    }

    QcBlockId door(const QString &facing, const QString &half, const QString &hinge) const
    {
        return state(u"qtcraft:oak_door[facing=%1,half=%2,open=true,hinge=%3]"_s.arg(facing, half, hinge));
    }

    QcBlockId trapdoor(const QString &facing) const { return state(u"qtcraft:oak_trapdoor[facing=%1,half=bottom,open=true]"_s.arg(facing)); }

    const QcAabb &panelOf(QcBlockId id) const { return m_content->state(id).collision.first(); }

    // Whether the panel of `id` stands along z (it spans the cell's whole depth in z).
    bool standsAlongZ(QcBlockId id) const { return panelOf(id).min.z <= 0.0 && panelOf(id).max.z >= 1.0; }

    std::unique_ptr<QcServerWorld> makeWorld() const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcFlatGenerator>(m_content, u"bedrock,3*stone,grass_block"_s));
        for (int x = -1; x <= 1; ++x) {
            for (int z = -1; z <= 1; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    // A stone wall three high across the way at `along`, open two high from across 0 to `width` - 1.
    void wall(QcServerWorld &world, const Scene &scene, int along, int width = 1) const
    {
        const QcBlockId stone = state(u"qtcraft:stone"_s);
        for (int across = -6; across <= 6; ++across) {
            for (int dy = 0; dy <= 2; ++dy) {
                if (dy == 2 || across < 0 || across >= width) {
                    world.setBlock(scene.cell(along, across, dy), stone);
                }
            }
        }
    }

    // Sets the open door or trapdoor `id` and notes its panel.
    void place(QcServerWorld &world, Scene &scene, QcBlockPos cell, QcBlockId id) const
    {
        world.setBlock(cell, id);
        const QcAabb &local = panelOf(id);
        const QcVec3 corner{static_cast<double>(cell.x), static_cast<double>(cell.y), static_cast<double>(cell.z)};
        scene.panels.push_back({local.min + corner, local.max + corner});
    }

    // A door of `hinge` with its lower half at `cell`, in its wall.
    void placeDoor(QcServerWorld &world, Scene &scene, QcBlockPos cell, const QString &facing, const QString &hinge) const
    {
        place(world, scene, cell, door(facing, u"bottom"_s, hinge));
        place(world, scene, cell.offset(0, 1, 0), door(facing, u"top"_s, hinge));
    }

    // The hinge side whose leaf stands at the low (`low`) or the high side across the way.
    QString hingeAt(const QString &facing, bool low) const
    {
        for (const QString &hinge : {u"left"_s, u"right"_s}) {
            const QcAabb &leaf = panelOf(door(facing, u"bottom"_s, hinge));
            const double side = standsAlongZ(door(facing, u"bottom"_s, hinge)) ? leaf.min.x : leaf.min.z;
            if ((side <= 0.0) == low) {
                return hinge;
            }
        }
        return {};
    }

    // Space between a body and a panel across the way while the body is beside it along the way.
    static std::optional<double> clearance(const QcAabb &body, const QcAabb &box, bool alongZ)
    {
        const auto low = [alongZ](const QcAabb &b, bool along) { return along == alongZ ? b.min.z : b.min.x; };
        const auto high = [alongZ](const QcAabb &b, bool along) { return along == alongZ ? b.max.z : b.max.x; };
        if (high(body, true) <= low(box, true) || low(body, true) >= high(box, true) || body.max.y <= box.min.y || body.min.y >= box.max.y) {
            return std::nullopt;
        }
        return std::max(low(box, false) - high(body, false), low(body, false) - high(box, false));
    }

    // The least space between a body of the creature `definition` at `position` and the panels
    // beside it along the way; none while it is beside none.
    static std::optional<double> nearest(const QcVec3 &position, const Scene &scene, const QcMobDefinition &definition)
    {
        const QcAabb body = QcPhysics::boxAt(position, definition.width, definition.height);
        std::optional<double> least;
        for (const QcAabb &box : scene.panels) {
            if (const std::optional<double> gap = clearance(body, box, scene.alongZ)) {
                least = std::min(least.value_or(*gap), *gap);
            }
        }
        return least;
    }

    // The creature m_walker starting at `start` hunts a player seven blocks beyond the wall at
    // along 0, on the other side, until it is `beyond` blocks past it; with m_push it is shoved
    // sideways once, a block and a half before the wall.
    Walk walk(QcServerWorld &world, const Scene &scene, const QcVec3 &start, double beyond = 2.0) const
    {
        const int direction = scene.alongOf(start) < 0.0 ? 1 : -1;
        QcMobManager mobs(m_content, 1);
        mobs.setNaturalSpawning(false);
        const int type = *m_content->mobIndex(u"qtcraft:"_s + m_walker);
        mobs.spawn(type, start, 1);
        QcCreature &mob = *mobs.find(1);
        mob.chasing = 7;
        mob.targetMemory = 1e9;
        mob.provokedBy = 7;
        mob.angerTimer = 1e9;
        const QList<QcMobManager::PlayerView> players{{7, scene.point(7.0 * direction, 0.0), true}};

        Walk walk;
        bool pushed = !m_push;
        quint32 nextId = 100;
        for (int tick = 0; tick < 20 * 10 && !walk.through; ++tick) {
            mobs.tick(world, players, midnight, dt, [&nextId] { return nextId++; });
            QcCreature &now = *mobs.find(1);
            walk.jumps += now.motion.velocity.y > 0.1 ? 1 : 0;
            walk.clearance = std::min(walk.clearance, nearest(now.motion.position, scene, m_content->mobs().at(type)).value_or(walk.clearance));
            if (!pushed && scene.alongOf(now.motion.position) * direction > -1.5) {
                (scene.alongZ ? now.motion.velocity.x : now.motion.velocity.z) += 4.0;
                pushed = true;
            }
            const double along = scene.alongOf(now.motion.position) * direction;
            if (along > -1.5 && along < -0.5) {
                walk.noteOffset(scene.acrossOf(now.motion.position) - scene.line());
            }
            walk.through = along > beyond;
            walk.ticks = tick + 1;
        }
        walk.searches = mobs.paths().searches();
        return walk;
    }

    static void expect(const Walk &walk, double clearance, const QString &what)
    {
        const QString seen = u"%1: through %2 in %5 ticks, %6 searches, jumps %3, clearance %4"_s.arg(what)
                                 .arg(walk.through).arg(walk.jumps).arg(walk.clearance).arg(walk.ticks).arg(walk.searches);
        QVERIFY2(walk.through, qPrintable(seen));
        QVERIFY2(walk.jumps == 0, qPrintable(seen));
        QVERIFY2(walk.clearance >= clearance, qPrintable(seen));
    }

    static void facingsAndDirections(bool hinges)
    {
        QTest::addColumn<QString>("facing");
        QTest::addColumn<QString>("hinge");
        QTest::addColumn<int>("direction");
        for (const QString &facing : {u"north"_s, u"east"_s, u"south"_s, u"west"_s}) {
            const QStringList sides = hinges ? QStringList{u"left"_s, u"right"_s} : QStringList{u"-"_s};
            for (const QString &hinge : sides) {
                for (const int direction : {1, -1}) {
                    QTest::addRow("%s-%s-%s", qPrintable(facing), qPrintable(hinge), direction > 0 ? "forth" : "back") << facing << hinge << direction;
                }
            }
        }
    }

private slots:
    void init()
    {
        m_walker = u"zombie"_s;
        m_push = false;
    }

    // Open doors and trapdoors narrow their cell along one axis to the opening beside the panel;
    // closed ones, open gates (no collision) and other blocks do not.
    void theGapLiesBesideThePanel()
    {
        const QcPathCellTable table(*m_content);
        int doors = 0;
        int trapdoors = 0;
        for (qsizetype index = 0; index < m_content->stateCount(); ++index) {
            const auto id = static_cast<QcBlockId>(index);
            const QcBlockDefinition &block = m_content->block(id);
            const QcPathGap gap = table.gap(id);
            const bool open = block.property(u"open"_s) && m_content->property(id, u"open"_s) == u"true"_s;
            const bool hasPanel = open && !m_content->state(id).collision.isEmpty();
            QVERIFY2((gap.x.has_value() ? 1 : 0) + (gap.z.has_value() ? 1 : 0) == (hasPanel ? 1 : 0), qPrintable(block.name));
            if (!hasPanel) {
                continue;
            }

            const QcPathOpening opening = gap.x ? *gap.x : *gap.z;
            const double middle = opening.middle();
            QVERIFY2(std::abs(opening.to - opening.from - (1.0 - panel)) < 1e-6, qPrintable(block.name));
            QVERIFY2(std::abs(middle - openingMiddle) < 1e-6 || std::abs(middle - (1.0 - openingMiddle)) < 1e-6, qPrintable(block.name));
            // A body 0.6 wide in the middle of the opening keeps clear of every box of the block.
            for (const QcAabb &box : m_content->state(id).collision) {
                const double apart = gap.x ? std::max(box.min.x - (middle + 0.3), (middle - 0.3) - box.max.x)
                                           : std::max(box.min.z - (middle + 0.3), (middle - 0.3) - box.max.z);
                QVERIFY2(apart > 0.1, qPrintable(block.name));
            }
            doors += block.door ? 1 : 0;
            trapdoors += block.name.contains(u"trapdoor"_s) ? 1 : 0;
        }
        QVERIFY(doors >= 2 * 4 * 2);
        QVERIFY(trapdoors >= 2 * 4);
    }

    void aCreatureKeepsClearOfTheLeaf_data() { facingsAndDirections(true); }

    // Through an open door from both sides, from every facing and hinge side: started five blocks
    // before it and off the line by a third of a block either way (not on the last bit of a float),
    // and steeply from the side, a block and a half before the wall and 2.3 blocks off the line (it
    // turns into the door from the cell right before it): the zombie keeps clear of the leaf.
    void aCreatureKeepsClearOfTheLeaf()
    {
        QFETCH(QString, facing);
        QFETCH(QString, hinge);
        QFETCH(int, direction);
        const QList<std::pair<double, double>> starts{{-5.0, 0.37}, {-5.0, -0.37}, {-1.4, 2.3}, {-1.4, -2.3}};
        for (const auto &[along, across] : starts) {
            auto world = makeWorld();
            Scene scene{standsAlongZ(door(facing, u"bottom"_s, hinge)), {}};
            wall(*world, scene, 0);
            placeDoor(*world, scene, scene.cell(0, 0, 0), facing, hinge);
            expect(walk(*world, scene, scene.point(along * direction, across)), minClearance, u"start %1 %2"_s.arg(along).arg(across));
        }
    }

    void throughAnOpenTrapdoor_data()
    {
        QTest::addColumn<QString>("facing");
        QTest::addColumn<int>("height");
        QTest::addColumn<int>("direction");
        for (const QString &facing : {u"north"_s, u"east"_s, u"south"_s, u"west"_s}) {
            for (const int height : {0, 1}) {
                for (const int direction : {1, -1}) {
                    QTest::addRow("%s-%s-%s", qPrintable(facing), height == 0 ? "feet" : "head", direction > 0 ? "forth" : "back")
                        << facing << height << direction;
                }
            }
        }
    }

    // An open trapdoor standing as a panel in a doorway two high, at the feet or at head height of a
    // zombie (1.95 high): it keeps clear of it like of a door's leaf.
    void throughAnOpenTrapdoor()
    {
        QFETCH(QString, facing);
        QFETCH(int, height);
        QFETCH(int, direction);
        for (const double across : {0.37, -0.37}) {
            auto world = makeWorld();
            Scene scene{standsAlongZ(trapdoor(facing)), {}};
            wall(*world, scene, 0);
            place(*world, scene, scene.cell(0, 0, height), trapdoor(facing));
            expect(walk(*world, scene, scene.point(-5.0 * direction, across)), minClearance, u"across %1"_s.arg(across));
        }
    }

    void throughADoubleDoor_data() { facingsAndDirections(false); }

    // A double door, both leaves at the outer sides: whichever door the zombie takes, it keeps clear
    // of its leaf.
    void throughADoubleDoor()
    {
        QFETCH(QString, facing);
        QFETCH(int, direction);
        for (const double across : {0.37, 0.63, 2.3, -2.3}) {
            auto world = makeWorld();
            Scene scene{standsAlongZ(door(facing, u"bottom"_s, u"left"_s)), {}};
            wall(*world, scene, 0, 2);
            placeDoor(*world, scene, scene.cell(0, 0, 0), facing, hingeAt(facing, true));
            placeDoor(*world, scene, scene.cell(0, 1, 0), facing, hingeAt(facing, false));
            expect(walk(*world, scene, scene.point(-5.0 * direction, across)), minClearance, u"across %1"_s.arg(across));
        }
    }

    void throughTwoDoorsHingedOppositeWays_data() { facingsAndDirections(true); }

    // Two doors right behind each other, hinged on opposite sides: the free way through both is
    // 10/16 wide, so a body 0.6 wide has 1.25 cm on each side while it is beside both leaves at once.
    // The zombie aims at the middle between them there instead of sliding into the first leaf
    // towards the second opening, and gets through without touching a leaf or jumping.
    void throughTwoDoorsHingedOppositeWays()
    {
        QFETCH(QString, facing);
        QFETCH(QString, hinge);
        QFETCH(int, direction);
        const QString other = hinge == u"left"_s ? u"right"_s : u"left"_s;
        for (const double across : {0.37, -0.37}) {
            auto world = makeWorld();
            Scene scene{standsAlongZ(door(facing, u"bottom"_s, hinge)), {}};
            wall(*world, scene, 0);
            wall(*world, scene, direction);
            placeDoor(*world, scene, scene.cell(0, 0, 0), facing, hinge);
            placeDoor(*world, scene, scene.cell(direction, 0, 0), facing, other);
            expect(walk(*world, scene, scene.point(-5.0 * direction, across), 3.0), 0.0, u"across %1"_s.arg(across));
        }
    }

    void aFastCreatureTurnsIntoTheDoor_data()
    {
        QTest::addColumn<QString>("walker");
        QTest::addColumn<QString>("facing");
        for (const QString &walker : {u"zombie"_s, u"wolf"_s}) {
            for (const QString &facing : {u"north"_s, u"east"_s, u"south"_s, u"west"_s}) {
                QTest::addRow("%s-%s", qPrintable(walker), qPrintable(facing)) << walker << facing;
            }
        }
    }

    // At chase speed (a zombie 0.16, a wolf 0.2 blocks a tick) along the wall, turning by a right
    // angle into the door at the cell before it, from places along the wall that leave it anywhere
    // between a hair and a step off the line when it comes there: it lines up without stepping
    // across the line and back again until it counts as stuck, and goes through clear of the leaf.
    void aFastCreatureTurnsIntoTheDoor()
    {
        QFETCH(QString, walker);
        QFETCH(QString, facing);
        m_walker = walker;
        for (const double along : {-1.17, -1.0, -0.87}) {
            for (const double across : {4.0, 4.03, 4.06, 4.09, 4.12, -4.0, -4.03, -4.06, -4.09, -4.12}) {
                auto world = makeWorld();
                Scene scene{standsAlongZ(door(facing, u"bottom"_s, u"left"_s)), {}};
                wall(*world, scene, 0);
                placeDoor(*world, scene, scene.cell(0, 0, 0), facing, u"left"_s);
                const Walk walk = this->walk(*world, scene, scene.point(along, across));
                expect(walk, minClearance, u"start %1 %2"_s.arg(along).arg(across));
                // Lined up on the way: it never stepped to and fro across the line until it counted
                // as stuck and searched again.
                QVERIFY2(walk.searches == 1, qPrintable(u"start %1 %2: %3 searches"_s.arg(along).arg(across).arg(walk.searches)));
                QVERIFY2(walk.crossings <= 1, qPrintable(u"start %1 %2: %3 crossings"_s.arg(along).arg(across).arg(walk.crossings)));
            }
        }
    }

    // Shoved sideways right before the door, the zombie still gets through (it does not wait for
    // ever to line up exactly).
    void aShovedCreatureStillGetsThrough()
    {
        m_push = true;
        for (const QString &facing : {u"north"_s, u"east"_s}) {
            auto world = makeWorld();
            Scene scene{standsAlongZ(door(facing, u"bottom"_s, u"right"_s)), {}};
            wall(*world, scene, 0);
            placeDoor(*world, scene, scene.cell(0, 0, 0), facing, u"right"_s);
            const Walk walk = this->walk(*world, scene, scene.point(-5.0, 0.0));
            QVERIFY2(walk.through, qPrintable(facing));
        }
    }
};

QTEST_GUILESS_MAIN(TestDoorSteering)
#include "test_door_steering.moc"
