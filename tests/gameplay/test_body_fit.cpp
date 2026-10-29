// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcBodyFit.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerWorld.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QTest>
#include <QtMath>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A pig: 0.9 m wide and high.
constexpr QcPhysics::BodySize pig{0.9, 0.9};
// High above the test terrain, where only the blocks a test sets stand.
constexpr int sky = 200;

} // namespace

namespace QTest {

template<>
char *toString(const QcVec3 &t)
{
    const QByteArray text = QByteArray::number(t.x) + ' ' + QByteArray::number(t.y) + ' ' + QByteArray::number(t.z);
    return qstrdup(text.constData());
}

} // namespace QTest

// Where QcBodyFit puts a body that is inside blocks (decision E-352).
class TestBodyFit : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcBlockId id(const char *name) const { return *m_content->blockId(u"qtcraft:"_s + QString::fromLatin1(name)); }

    std::unique_ptr<QcServerWorld> makeWorld(int radius = 1) const
    {
        auto world = std::make_unique<QcServerWorld>(m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        for (int x = -radius; x <= radius; ++x) {
            for (int z = -radius; z <= radius; ++z) {
                world->loadChunk({x, z});
            }
        }
        return world;
    }

    void stone(QcServerWorld &world, QcBlockPos pos) const { world.setBlock(pos, id("stone")); }

    // Walks a body into each target from 64 directions for 40 ticks; counts the ticks it overlaps.
    static int walkInto(const QcServerWorld &world, const QList<QcBlockPos> &targets, QcPhysics::BodySize body)
    {
        int overlaps = 0;
        for (const QcBlockPos target : targets) {
            for (int i = 0; i < 64; ++i) {
                const double angle = i * (2.0 * M_PI / 64.0);
                QcEntityMotion motion;
                motion.position = {target.x + 0.5 + std::sin(angle) * 1.6, static_cast<double>(sky), target.z + 0.5 - std::cos(angle) * 1.6};
                const QcVec3 towards = QcVec3{target.x + 0.5 - motion.position.x, 0.0, target.z + 0.5 - motion.position.z}.normalized();
                for (int tick = 0; tick < 40; ++tick) {
                    motion.step({towards, 1.0, false}, body, world, QcPhysics::tickDuration);
                    overlaps += QcBodyFit::overlapsBlocks(world, motion.position, body, QcBodyFit::Unloaded::Clear) ? 1 : 0;
                }
            }
        }
        return overlaps;
    }

private slots:
    void clearBodyStays()
    {
        auto world = makeWorld();
        const QcVec3 feet{0.5, sky + 0.25, 0.5};
        const std::optional<QcVec3> clear = QcBodyFit::clearFeet(*world, feet, pig);
        QVERIFY(clear.has_value());
        QCOMPARE(*clear, feet);
    }

    void upSnapsOntoTheTopFace()
    {
        auto world = makeWorld();
        stone(*world, {0, sky, 0});
        const std::optional<QcVec3> clear = QcBodyFit::clearFeet(*world, {0.5, sky + 0.25, 0.5}, pig);
        QVERIFY(clear.has_value());
        const QcVec3 expected{0.5, sky + 1.0, 0.5};
        QCOMPARE(*clear, expected);
    }

    void sidesInTheOrderNorthEastSouthWestThenDown()
    {
        // Under a ceiling: the first free side wins, in a fixed order, then one metre down.
        auto world = makeWorld();
        const QcVec3 feet{0.5, static_cast<double>(sky), 0.5};
        stone(*world, {0, sky, 0});
        stone(*world, {0, sky + 1, 0});
        const QList<QcVec3> expected{feet + QcVec3{0.0, 0.0, -1.0}, feet + QcVec3{1.0, 0.0, 0.0}, feet + QcVec3{0.0, 0.0, 1.0},
                                     feet + QcVec3{-1.0, 0.0, 0.0}, feet + QcVec3{0.0, -1.0, 0.0}};
        for (const QcVec3 &place : expected) {
            const std::optional<QcVec3> clear = QcBodyFit::clearFeet(*world, feet, pig);
            QVERIFY(clear.has_value());
            QCOMPARE(*clear, place);
            stone(*world, QcPhysics::blockContaining(place));
        }

        const bool nowhere = !QcBodyFit::clearFeet(*world, feet, pig).has_value();
        QVERIFY(nowhere);
    }

    void neverOntoTheRoofOfAOneBlockPen()
    {
        // A pig in a pen one block high with a block put where it stands: the roof is clear above
        // the ceiling, but it does not get there; boxed in, it has nowhere to go.
        auto world = makeWorld();
        stone(*world, {0, sky, 0});
        stone(*world, {0, sky + 1, 0});
        stone(*world, {0, sky - 1, 0});
        for (const QcBlockPos side : {QcBlockPos{0, sky, -1}, QcBlockPos{1, sky, 0}, QcBlockPos{0, sky, 1}, QcBlockPos{-1, sky, 0}}) {
            stone(*world, side);
        }
        const bool nowhere = !QcBodyFit::clearFeet(*world, {0.5, sky + 0.25, 0.5}, pig).has_value();
        QVERIFY(nowhere);
    }

    void fencesReachIntoTheCellAbove()
    {
        // A fence post is 1.5 m high: a body standing 1.2 m above the fence's cell is inside it.
        auto world = makeWorld();
        world->setBlock({0, sky, 0}, id("oak_fence"));
        const bool inside = QcBodyFit::overlapsBlocks(*world, {0.5, sky + 1.2, 0.5}, pig, QcBodyFit::Unloaded::Clear);
        const bool above = QcBodyFit::overlapsBlocks(*world, {0.5, sky + 1.5, 0.5}, pig, QcBodyFit::Unloaded::Clear);
        QVERIFY(inside);
        QVERIFY(!above);
    }

    void walkingIntoPartialShapesNeverCountsAsInside()
    {
        // Pigs walking into a fence post, a wall, a slab and a piston from many places within a
        // cell: the clipped moves leave them touching, never inside (no relocation).
        auto world = makeWorld();
        const int floorY = sky - 1;
        for (int x = -6; x <= 6; ++x) {
            for (int z = -6; z <= 6; ++z) {
                stone(*world, {x, floorY, z});
            }
        }
        world->setBlock({0, sky, 0}, id("oak_fence"));
        world->setBlock({3, sky, 0}, id("cobblestone_wall"));
        world->setBlock({0, sky, 3}, id("oak_slab"));
        world->setBlock({-3, sky, 0}, id("signal_piston"));

        int relocations = 0;
        const QList<QcBlockPos> targets{{0, sky, 0}, {3, sky, 0}, {0, sky, 3}, {-3, sky, 0}};
        // The body sizes of the base game: zombie, pig, slime, firefly.
        for (const QcPhysics::BodySize body : {QcPhysics::BodySize{0.6, 1.95}, pig, QcPhysics::BodySize{0.8, 0.8}, QcPhysics::BodySize{0.25, 0.25}}) {
            relocations += walkInto(*world, targets, body);
        }
        QCOMPARE(relocations, 0);
    }

    void unloadedCellsBlockEveryPlaceButTheStart()
    {
        // Only chunk 0,0 is loaded: the body at its east edge may stay half in the unloaded chunk,
        // but is never moved into it.
        auto world = makeWorld(0);
        const QcVec3 edge{15.8, sky + 0.25, 8.5};
        const std::optional<QcVec3> stays = QcBodyFit::clearFeet(*world, edge, pig);
        QVERIFY(stays.has_value());
        QCOMPARE(*stays, edge);

        const QcVec3 feet{15.5, sky + 0.25, 8.5};
        stone(*world, {15, sky, 8});
        stone(*world, {15, sky + 1, 8});
        stone(*world, {15, sky, 7});
        const std::optional<QcVec3> clear = QcBodyFit::clearFeet(*world, feet, pig);
        QVERIFY(clear.has_value());
        const QcVec3 south = feet + QcVec3{0.0, 0.0, 1.0};
        QCOMPARE(*clear, south);
    }

    void neverBelowTheWorldBottom()
    {
        // Deep in the rock at y 0 the only open way leads below the world: no place.
        auto world = makeWorld();
        const bool nowhere = !QcBodyFit::clearFeet(*world, {0.5, 0.0, 0.5}, pig).has_value();
        QVERIFY(nowhere);
    }

    void atTheWorldTopTheBodyStandsOnTheLastBlock()
    {
        auto world = makeWorld();
        const int top = QcWorld::worldHeight - 1;
        stone(*world, {0, top, 0});
        const std::optional<QcVec3> clear = QcBodyFit::clearFeet(*world, {0.5, top + 0.25, 0.5}, pig);
        QVERIFY(clear.has_value());
        const QcVec3 onTop{0.5, static_cast<double>(QcWorld::worldHeight), 0.5};
        QCOMPARE(*clear, onTop);
    }
};

QTEST_GUILESS_MAIN(TestBodyFit)
#include "test_body_fit.moc"
