#include "gameplay/QcBoatFixture.h"

#include <cmath>

using namespace Qt::StringLiterals;

// Boats on the server (decision E-920), with a raw protocol client: a player places a boat on a pond,
// gets in, drives it with his inputs exactly as the shared physics predicts on his client, gets out
// onto the bank and breaks it; a boat picks up a creature that touches it and carries it, but never
// a fish; a boat goes with its chunk and comes back.
class TestBoats : public QcBoatFixture
{
    Q_OBJECT

private slots:
    void playerPlacesDrivesLeavesAndBreaksABoat()
    {
        const quint32 id = placeBoatOnPond();
        QCOMPARE(m_server->entities().boats().all().size(), 1);
        QCOMPARE(countOf(item("qtcraft:oak_boat")), 0);
        QVERIFY(std::abs(boat(id).motion.position.y - floatingHeight()) < 1e-9);
        QCOMPARE(boat(id).motion.position.x, m_feet.x + 3.5);
        QCOMPARE(boat(id).yaw, 90.0f);

        // In: the state names the boat and carries its state; the player sits on the driver's seat.
        send(QcMsg::UseEntity{id});
        idle(1);
        QCOMPARE(m_client->vehicle, id);
        QVERIFY(m_client->boat.has_value());
        QCOMPARE(boat(id).driver, m_client->entityId);
        const QcVec3 seat = QcBoatBody::driverSeat(boat(id).motion.position, boat(id).yaw);
        QVERIFY((m_client->motion.position - seat).length() < 1e-9);
        QVERIFY(entityProblems().isEmpty());

        // Turning a little to and fro and paddling: every input steps the boat as the client
        // predicts it from the state it was sent, input by input (no correction).
        QcTestUtil::pump();
        QCOMPARE(*m_client->boat, boat(id).state());
        QcBoatState predicted = *m_client->boat;
        const auto drive = [&](float forward, float strafe, int count) {
            for (int i = 0; i < count; ++i) {
                input(forward, strafe);
                QcPlayerInput in;
                in.forward = forward;
                in.strafe = strafe;
                predicted.step(QcBoatControls::of(in), m_server->world());
            }
        };
        drive(0.0f, 1.0f, 3);
        drive(0.0f, -1.0f, 3);
        drive(1.0f, 0.0f, 20);
        QCOMPARE(boat(id).state(), predicted);
        QCOMPARE(*m_client->boat, predicted);
        QVERIFY(boat(id).motion.inWater);
        QVERIFY(std::abs(boat(id).motion.position.y - floatingHeight()) < 0.01);
        QVERIFY2(boat(id).motion.position.x > m_feet.x + 6.0, qPrintable(QString::number(boat(id).motion.position.x - m_feet.x)));
        QVERIFY((m_client->motion.position - QcBoatBody::driverSeat(boat(id).motion.position, boat(id).yaw)).length() < 1e-9);

        // Nobody else moves a driven boat: ticks without input leave it where it is.
        const QcVec3 parked = boat(id).motion.position;
        ticks(5);
        QCOMPARE(boat(id).motion.position, parked);

        // Paddled on against the east bank, sneak gets the player out onto it (dry ground, not water).
        drive(1.0f, 0.0f, 40);
        QVERIFY2(boat(id).motion.position.x + QcBoatBody::width * 0.5 > m_feet.x + 14.99, qPrintable(QString::number(boat(id).motion.position.x - m_feet.x)));
        drive(0.0f, 0.0f, 10);
        QCOMPARE(boat(id).state(), predicted);
        input(0.0f, 0.0f, true);
        idle(1);
        QCOMPARE(m_client->vehicle, 0u);
        QVERIFY(!m_client->boat);
        QCOMPARE(boat(id).driver, 0u);
        const QcBlockPos standing = QcPhysics::blockContaining(m_client->motion.position);
        QVERIFY2(!QcPhysics::isLiquidAt(m_server->world(), standing), qPrintable(QString::number(m_client->motion.position.y)));
        QVERIFY(!QcPhysics::collisionBoxesAt(m_server->world(), standing.offset(0, -1, 0)).isEmpty());

        // A hit breaks it; it drops its item.
        idle(10);
        send(QcMsg::AttackEntity{id});
        QVERIFY(m_server->entities().boats().all().isEmpty());
        QCOMPARE(droppedCount(item("qtcraft:oak_boat")), 1);
        QVERIFY(entityProblems().isEmpty());
    }

    void aBoatFallsWhereThereIsNoWaterAndRestsOnLand()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        const QcBoat *placed = spawnBoat({m_feet.x + 2.5, m_feet.y + 3.0, m_feet.z + 0.5}, 0.0f);
        QVERIFY(placed);
        const quint32 id = placed->id;
        ticks(40);
        QCOMPARE(boat(id).motion.position.y, double(m_feet.y));
        QVERIFY(boat(id).motion.onGround);
        QVERIFY(!boat(id).motion.inWater);
    }

    void aBoatPicksUpACreatureAndCarriesIt()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        digPond();
        // A boat on the pond at its edge and a pig on the bank beside it, touching it.
        const QcBoat *placed = spawnBoat({m_feet.x + 2.7, floatingHeight(), m_feet.z + 0.5}, 90.0f);
        QVERIFY(placed);
        const quint32 id = placed->id;
        const quint32 pig = spawnCreature(u"pig"_s, {m_feet.x + 1.5, static_cast<double>(m_feet.y), m_feet.z + 0.5});
        QVERIFY(pig != 0);
        ticks(2);
        QCOMPARE(boat(id).passenger, pig);
        const QcCreature *sitting = m_server->entities().creatures().find(pig);
        QCOMPARE(sitting->vehicle, id);
        QCOMPARE(sitting->pose, QcPose::Sitting);
        QVERIFY((sitting->motion.position - QcBoatBody::passengerSeat(boat(id).motion.position, boat(id).yaw)).length() < 1e-9);

        // The player drives off; the pig goes along in its seat and thinks no step of its own.
        send(QcMsg::UseEntity{id});
        idle(1);
        QCOMPARE(m_client->vehicle, id);
        for (int i = 0; i < 25; ++i) {
            input(1.0f, 0.0f);
        }
        const QcCreature *carried = m_server->entities().creatures().find(pig);
        QVERIFY(boat(id).motion.position.x > m_feet.x + 6.0);
        QVERIFY((carried->motion.position - QcBoatBody::passengerSeat(boat(id).motion.position, boat(id).yaw)).length() < 1e-9);
        QVERIFY(entityProblems().isEmpty());

        // Broken, the boat lets the pig out: it stands and walks again.
        input(0.0f, 0.0f, true);
        idle(10);
        send(QcMsg::AttackEntity{id});
        QVERIFY(m_server->entities().boats().all().isEmpty());
        const QcCreature *free = m_server->entities().creatures().find(pig);
        QCOMPARE(free->vehicle, 0u);
        QCOMPARE(free->pose, QcPose::Standing);
    }

    void fishAndWideCreaturesStayOut()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        digPond();
        const QcBoat *placed = spawnBoat({m_feet.x + 6.5, floatingHeight(), m_feet.z + 0.5}, 0.0f);
        QVERIFY(placed);
        const quint32 id = placed->id;
        const quint32 cod = spawnCreature(u"cod"_s, {m_feet.x + 6.5, m_feet.y - 1.5, m_feet.z + 1.2});
        QVERIFY(cod != 0);
        ticks(3);
        QCOMPARE(boat(id).passenger, 0u);
        QVERIFY(!m_server->systems().boats.fits(*m_server->entities().creatures().find(cod)));
        const quint32 golem = spawnCreature(u"iron_golem"_s, {m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 2.5});
        QVERIFY(!m_server->systems().boats.fits(*m_server->entities().creatures().find(golem)));
    }

    void aBoatLeavesWithItsChunkAndComesBack()
    {
        m_unloadChunks = true;
        join();
        chat(u"/gamerule mobSpawning false"_s);
        // Far away, in chunks nobody needs: a boat resting on the ground.
        const QcBlockPos far = m_feet.offset(6 * QcWorld::chunkSize, 0, 0);
        m_server->world().loadChunk(far.chunk());
        for (int dy = 0; dy < 4; ++dy) {
            m_server->world().setBlock(far.offset(0, dy, 0), qcAirBlockId);
        }
        m_server->world().setBlock(far.offset(0, -1, 0), id("qtcraft:stone"));
        const QcBoat *placed = spawnBoat({far.x + 0.5, static_cast<double>(far.y), far.z + 0.5}, 123.0f);
        QVERIFY(placed);
        const QcVec3 where = placed->motion.position;
        const QcChunkPos chunk = QcPhysics::blockContaining(where).chunk();
        for (int i = 0; i < 400 && m_server->world().chunk(chunk); ++i) {
            ticks(1);
        }
        QVERIFY(!m_server->world().chunk(chunk));
        QVERIFY(m_server->entities().boats().all().isEmpty());

        m_server->world().loadChunk(chunk);
        QCOMPARE(m_server->entities().boats().all().size(), 1);
        const QcBoat &back = m_server->entities().boats().all().constFirst();
        QCOMPARE(back.motion.position, where);
        QCOMPARE(back.yaw, 123.0f);
        QCOMPARE(back.item, u"qtcraft:oak_boat"_s);
        QVERIFY(entityProblems().isEmpty());
        m_unloadChunks = false;
    }
};

QTEST_GUILESS_MAIN(TestBoats)
#include "test_boats.moc"
