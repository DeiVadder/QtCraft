#include "common/physics/QcPhysics.h"
#include "QcCircuitFixture.h"
#include "server/gameplay/QcEnvironmentDamage.h"
#include "server/gameplay/QcServerCombat.h"

using namespace Qt::StringLiterals;

// Bodies pushed by Signal Pistons (decision E-337): pushes stop at solid blocks, a body left inside
// a block goes to the nearest free cell (up first), and suffocates only when there is none; pushes
// from two opposite heads cancel out the same way in every rotation.
class TestSignalBodies : public QcCircuitFixture
{
    Q_OBJECT

private:
    void teleport(const QcVec3 &feet)
    {
        chat(u"/tp %1 %2 %3"_s.arg(feet.x).arg(feet.y).arg(feet.z));
        idle(2);
    }

    QcVec3 centreOf(int x, int y, int z) const
    {
        const QcBlockPos cell = at(x, y, z);
        return {cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
    }

    // Whether a box overlaps any solid collision box of the world.
    bool insideSolid(const QcAabb &box) const
    {
        for (int x = static_cast<int>(std::floor(box.min.x)); x <= static_cast<int>(std::floor(box.max.x - 1e-6)); ++x) {
            for (int y = static_cast<int>(std::floor(box.min.y)); y <= static_cast<int>(std::floor(box.max.y - 1e-6)); ++y) {
                for (int z = static_cast<int>(std::floor(box.min.z)); z <= static_cast<int>(std::floor(box.max.z - 1e-6)); ++z) {
                    const QList<QcAabb> solids = QcPhysics::collisionBoxesAt(m_server->world(), {x, y, z});
                    if (std::ranges::any_of(solids, [&box](const QcAabb &solid) { return solid.intersects(box); })) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    QcAabb playerBox() const { return QcPhysics::boxAt(m_client->motion.position, QcPhysics::playerWidth, QcPhysics::playerHeight); }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void wallStopsThePushAndTheBodyClimbsOut()
    {
        buildArea();
        // The stone moves into the player's cell; a wall right behind leaves no room ahead.
        put(0, 0, 5, u"lever[attach=floor]"_s);
        put(1, 0, 5, u"signal_piston[facing=east]"_s);
        put(2, 0, 5, u"stone"_s);
        put(4, 0, 5, u"stone_bricks"_s);
        put(4, 1, 5, u"stone_bricks"_s);
        ticks(1);
        teleport(centreOf(3, 0, 5));

        flip(0, 0, 5);
        idle(2);
        const QcVec3 feet = m_client->motion.position;
        QVERIFY2(feet.x < at(4, 0, 5).x, qPrintable(QString::number(feet.x)));
        QVERIFY2(feet.y >= at(3, 1, 5).y - 0.01, qPrintable(QString::number(feet.y)));
        QVERIFY(!insideSolid(playerBox()));
    }

    void creaturesAndItemsStopAtWalls()
    {
        buildArea();
        put(0, 0, 8, u"lever[attach=floor]"_s);
        put(1, 0, 8, u"signal_piston[facing=east]"_s);
        put(2, 0, 8, u"stone"_s);
        for (int y = 0; y < 3; ++y) {
            put(4, y, 8, u"stone_bricks"_s);
        }
        ticks(1);

        const quint32 pig = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:pig"_s), centreOf(3, 0, 8), 700001, false, true);
        const quint32 dropped = m_server->items().spawnScattered({item("qtcraft:stick"), 1, 0}, centreOf(3, 0, 8), 700002);
        for (QcItemEntity &entity : m_server->items().editableItems()) {
            entity.motion.position = entity.id == dropped ? centreOf(3, 0, 8) : entity.motion.position;
            entity.motion.velocity = {};
        }

        flip(0, 0, 8);
        ticks(1);
        const QcMobDefinition &definition = m_content->mobs()[m_server->mobs().find(pig)->type];
        const QcVec3 pigFeet = m_server->mobs().find(pig)->motion.position;
        QVERIFY(!insideSolid(QcPhysics::boxAt(pigFeet, definition.width, definition.height)));
        QVERIFY(pigFeet.x < at(4, 0, 8).x);
        const auto stick = std::ranges::find(m_server->items().items(), dropped, &QcItemEntity::id);
        QVERIFY(stick != m_server->items().items().end());
        QVERIFY(!insideSolid(QcPhysics::boxAt(stick->motion.position, QcItemEntityManager::itemSize, QcItemEntityManager::itemSize)));
        QVERIFY(stick->motion.position.x < at(4, 0, 8).x);
    }

    void downwardPistonNeverPushesThroughTheFloor()
    {
        buildArea();
        // A piston pushes a stone down into the player's head on a one-block floor, walled in on
        // all four sides: the player cannot go anywhere and suffocates instead of falling through.
        put(5, 3, 5, u"signal_piston[facing=down]"_s);
        put(5, 4, 5, u"lever[attach=floor]"_s);
        put(5, 2, 5, u"stone"_s);
        for (const auto &[dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            put(5 + dx, 0, 5 + dz, u"stone_bricks"_s);
            put(5 + dx, 1, 5 + dz, u"stone_bricks"_s);
        }
        ticks(1);
        teleport(centreOf(5, 0, 5));
        const double health = 20.0;

        flip(5, 4, 5);
        idle(QcEnvironmentDamage::suffocationIntervalTicks * 2 + 2);
        QVERIFY2(m_client->motion.position.y >= at(5, 0, 5).y - 0.01, qPrintable(QString::number(m_client->motion.position.y)));
        QVERIFY(isBlock(5, 1, 5, "stone"));
        QVERIFY2(m_client->health < health, qPrintable(QString::number(m_client->health)));
    }

    void downwardPistonMovesAsideWhenItCan()
    {
        buildArea();
        put(5, 2, 5, u"signal_piston[facing=down]"_s);
        put(5, 3, 5, u"lever[attach=floor]"_s);
        ticks(1);
        teleport(centreOf(5, 0, 5) + QcVec3{0.1, 0.0, 0.0});

        flip(5, 3, 5);
        idle(2);
        QVERIFY(!insideSolid(playerBox()));
        QVERIFY2(std::abs(m_client->motion.position.y - at(5, 0, 5).y) < 0.01, qPrintable(QString::number(m_client->motion.position.y)));
        QCOMPARE(m_client->health, 20.0);
    }

    void downwardPushIntoOpenSpaceGoesDown()
    {
        buildArea();
        // No floor under the item: the push takes it down, nothing holds it.
        put(5, 3, 5, u"signal_piston[facing=down]"_s);
        put(5, 4, 5, u"lever[attach=floor]"_s);
        put(5, 2, 5, u"stone"_s);
        putAt(at(5, -1, 5), qcAirBlockId);
        ticks(1);
        const QcVec3 start = centreOf(5, 1, 5) + QcVec3{0.0, 0.5, 0.0};
        const quint32 dropped = m_server->items().spawnScattered({item("qtcraft:stick"), 1, 0}, start, 700003);
        for (QcItemEntity &entity : m_server->items().editableItems()) {
            entity.motion.position = entity.id == dropped ? start : entity.motion.position;
            entity.motion.velocity = {};
        }

        flip(5, 4, 5);
        ticks(1);
        const auto stick = std::ranges::find(m_server->items().items(), dropped, &QcItemEntity::id);
        QVERIFY(stick != m_server->items().items().end());
        QVERIFY2(stick->motion.position.y < at(5, 1, 5).y, qPrintable(QString::number(stick->motion.position.y)));
    }

    void opposingHeadsCancelInEveryTurn()
    {
        buildArea();
        QList<QcVec3> ends;
        for (int turns = 0; turns < 4; ++turns) {
            clearArea(m_origin);
            m_turns = turns;
            // Two heads come into the cells on both sides of the item at once.
            put(1, 0, 6, u"signal_piston[facing=east]"_s);
            put(4, 0, 6, u"signal_piston[facing=west]"_s);
            put(1, 0, 7, u"lever[attach=floor]"_s);
            put(4, 0, 7, u"lever[attach=floor]"_s);
            ticks(1);
            const QcBlockPos between = at(2, 0, 6);
            const QcBlockPos other = at(3, 0, 6);
            const QcVec3 start{(between.x + other.x) * 0.5 + 0.5, static_cast<double>(between.y), (between.z + other.z) * 0.5 + 0.5};
            m_server->items().removeWhere([](const QcItemEntity &) { return true; });
            const quint32 dropped = m_server->items().spawnScattered({item("qtcraft:stick"), 1, 0}, start, 710000 + static_cast<quint32>(turns));
            for (QcItemEntity &entity : m_server->items().editableItems()) {
                entity.motion.position = entity.id == dropped ? start : entity.motion.position;
                entity.motion.velocity = {};
            }

            flip(1, 0, 7);
            flip(4, 0, 7);
            ticks(1);
            const auto stick = std::ranges::find(m_server->items().items(), dropped, &QcItemEntity::id);
            QVERIFY(stick != m_server->items().items().end());
            ends.append(stick->motion.position - start);
        }

        // Both pushes together leave it between the heads; it climbs out on top in every turn.
        for (const QcVec3 &end : std::as_const(ends)) {
            QVERIFY2(std::abs(end.x) < 1e-6 && std::abs(end.z) < 1e-6, qPrintable(u"%1 %2"_s.arg(end.x).arg(end.z)));
            QVERIFY2(std::abs(end.y - ends.constFirst().y) < 1e-6, qPrintable(QString::number(end.y)));
        }
    }
};

QTEST_GUILESS_MAIN(TestSignalBodies)
#include "test_signal_bodies.moc"
