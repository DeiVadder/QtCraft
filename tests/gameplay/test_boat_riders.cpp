// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gameplay/QcBoatFixture.h"
#include "server/storage/QcEntityRecords.h"

#include <QTemporaryDir>

#include <cmath>

using namespace Qt::StringLiterals;

// Drivers, passengers and resting boats on the server (decision E-920): a driver moved off his seat
// (a teleport, a knock) is out of the boat and his inputs move him from there; a player in a boat
// gets into no cart and no other player's boat; a burst of inputs beyond the budget moves the boat
// only by the inputs taken; a driver saved while driving comes back on the exit spot, a passenger
// after a reload sits in the boat again; placing beyond the limit is refused; a creature whose boat
// left with its chunk stands up; boats nobody drives come to rest and wake when something changes.
class TestBoatRiders : public QcBoatFixture
{
    Q_OBJECT

private:
    // Places a boat on the pond and gets in; its id.
    quint32 drive()
    {
        const quint32 id = placeBoatOnPond();
        send(QcMsg::UseEntity{id});
        idle(1);
        return id;
    }

    void rejoin()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
        ticks(2);
    }

    // Stops the server (it saves the world) and opens the world again with the player back in it.
    void restart(QcServerConfig config)
    {
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = m_viewDistance;
        config.keepChunksLoaded = true;
        config.operators = {u"Raw"_s};
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        rejoin();
    }

    // The x of the chunk border nearest to the player's feet, on a flat floor reaching three blocks
    // to either side of it.
    int chunkBorderOnFloor()
    {
        const int below = m_feet.x - (((m_feet.x % QcWorld::chunkSize) + QcWorld::chunkSize) % QcWorld::chunkSize);
        const int border = m_feet.x - below < QcWorld::chunkSize / 2 ? below : below + QcWorld::chunkSize;
        for (int x = border - 3; x <= border + 3; ++x) {
            for (int dz = -2; dz <= 2; ++dz) {
                const QcBlockPos ground{x, m_feet.y - 1, m_feet.z + dz};
                m_server->world().setBlock(ground, id("qtcraft:grass_block"));
                for (int dy = 1; dy <= 3; ++dy) {
                    m_server->world().setBlock(ground.offset(0, dy, 0), qcAirBlockId);
                }
            }
        }
        return border;
    }

    struct Seated
    {
        quint32 boat = 0;
        quint32 pig = 0;
    };
    // A boat on the ground just west of the chunk border, pointing east, with a pig on its front
    // seat beyond the border.
    Seated pigAcrossTheBorder(int border)
    {
        const QcBoat *placed = spawnBoat({border - 0.3, static_cast<double>(m_feet.y), m_feet.z + 0.5}, 90.0f);
        if (!placed) {
            QcTestUtil::failCase(u"no boat"_s);
            return {};
        }
        const quint32 boatId = placed->id;
        const QcVec3 seat = QcBoatBody::passengerSeat(placed->motion.position, placed->yaw);
        const quint32 pig = spawnCreature(u"pig"_s, {seat.x, static_cast<double>(m_feet.y), seat.z});
        ticks(2);
        return {boatId, pig};
    }

private slots:
    void aDriverMovedOffHisSeatIsOutOfTheBoat()
    {
        const quint32 id = drive();
        QCOMPARE(boat(id).driver, m_client->entityId);

        // Teleported: the next input moves him from the target, the boat stays.
        const QcVec3 parked = boat(id).motion.position;
        chat(u"/tp %1 %2 %3"_s.arg(m_feet.x).arg(m_feet.y).arg(m_feet.z - 2));
        input(1.0f, 0.0f);
        idle(1);
        QCOMPARE(boat(id).driver, 0u);
        QCOMPARE(m_client->vehicle, 0u);
        QVERIFY(!m_client->boat);
        QVERIFY2(std::abs(m_client->motion.position.z - (m_feet.z - 2)) < 1.0, qPrintable(QString::number(m_client->motion.position.z - m_feet.z)));
        QVERIFY(std::abs(m_client->motion.position.x - m_feet.x) < 1.0);
        QCOMPARE(boat(id).motion.position.x, parked.x);

        // Knocked off the seat (anything that moves him more than the seat's tolerance) before his
        // next input: that input is no longer the boat's.
        send(QcMsg::UseEntity{id});
        idle(1);
        QCOMPARE(boat(id).driver, m_client->entityId);
        const QcVec3 knocked = player().motion.position + QcVec3{0.0, 0.0, -1.5};
        player().motion.position = knocked;
        input(1.0f, 0.0f);
        QCOMPARE(boat(id).driver, 0u);
        QVERIFY((player().motion.position - knocked).length() < 1.0);
    }

    void aPlayerInABoatGetsIntoNoCartAndNoOtherBoat()
    {
        const quint32 id = drive();
        QcMinecart standing = m_server->carts().fromItem(u"qtcraft:minecart"_s);
        standing.motion.position = {m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 1.5};
        const QcMinecart *cart = m_server->systems().minecarts.spawn(standing);
        QVERIFY(cart);
        const quint32 cartId = cart->id;
        send(QcMsg::UseEntity{cartId});
        idle(1);
        QCOMPARE(m_server->carts().find(cartId)->rider, 0u);
        QCOMPARE(boat(id).driver, m_client->entityId);
        QCOMPARE(m_client->vehicle, id);

        // Another boat beside it: refused as well.
        const QcBoat *other = spawnBoat({m_feet.x + 3.5, floatingHeight(), m_feet.z + 2.0}, 0.0f);
        QVERIFY(other);
        const quint32 otherId = other->id;
        send(QcMsg::UseEntity{otherId});
        idle(1);
        QCOMPARE(boat(otherId).driver, 0u);
        QCOMPARE(boat(id).driver, m_client->entityId);

        // A second player cannot take the driven boat; once it is free he can.
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        RawClient bob(std::move(clientEnd));
        bob.hello(u"Bob"_s);
        QcTestUtil::pump();
        ticks(2);
        bob.connection.send(QcMsg::UseEntity{id});
        QcTestUtil::pump();
        ticks(1);
        QCOMPARE(boat(id).driver, m_client->entityId);
        input(0.0f, 0.0f, true);
        QCOMPARE(boat(id).driver, 0u);
        bob.connection.send(QcMsg::UseEntity{id});
        QcTestUtil::pump();
        ticks(1);
        QCOMPARE(boat(id).driver, bob.entityId);
    }

    void aSpectatorLeavesHisBoatOnTheExitSpot()
    {
        const quint32 id = drive();
        const QcVec3 exit = QcServerBoats::exitSpot(m_server->world(), boat(id));
        chat(u"/gamemode spectator"_s);
        ticks(1);
        QCOMPARE(boat(id).driver, 0u);
        QCOMPARE(player().motion.position, exit);
    }

    void aBurstOfInputsMovesTheBoatOnlyByTheInputsTaken()
    {
        const quint32 id = drive();
        QcTestUtil::pump();
        QCOMPARE(*m_client->boat, boat(id).state());
        QcBoatState start = boat(id).state();
        const quint32 first = m_sequence + 1;
        constexpr int burst = 40;
        for (int i = 0; i < burst; ++i) {
            QcPlayerInput in;
            in.sequence = ++m_sequence;
            in.forward = 1.0f;
            in.yaw = 90.0f;
            m_client->connection.send(QcMsg::PlayerInput{in});
        }
        QcTestUtil::pump();
        ticks(1);
        QcTestUtil::pump();

        const int taken = static_cast<int>(m_client->acknowledged - first + 1);
        QVERIFY2(taken >= 1 && taken < burst, qPrintable(QString::number(taken)));
        QcPlayerInput paddle;
        paddle.forward = 1.0f;
        for (int i = 0; i < taken; ++i) {
            start.step(QcBoatControls::of(paddle), m_server->world());
        }
        QCOMPARE(boat(id).state(), start);
        QCOMPARE(*m_client->boat, start);
    }

    void aDriverIsSavedOnTheExitSpotAndAPassengerSitsAgainAfterAReload()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        digPond();
        const QcBoat *placed = spawnBoat({m_feet.x + 2.7, floatingHeight(), m_feet.z + 0.5}, 90.0f);
        QVERIFY(placed);
        const quint32 id = placed->id;
        const auto it = std::ranges::find(m_content->mobs(), u"qtcraft:pig"_s, &QcMobDefinition::name);
        const quint32 pig = m_server->entities().creatures().spawn(static_cast<int>(it - m_content->mobs().begin()),
                                                                   {m_feet.x + 1.5, static_cast<double>(m_feet.y), m_feet.z + 0.5},
                                                                   m_server->entities().newId(), false, true);
        ticks(2);
        QCOMPARE(boat(id).passenger, pig);
        send(QcMsg::UseEntity{id});
        idle(1);
        QCOMPARE(boat(id).driver, m_client->entityId);
        const QcVec3 exit = QcServerBoats::exitSpot(m_server->world(), boat(id));

        // Saved while driving (the server stops): he comes back on the exit spot, out of the boat.
        restart(config);
        QCOMPARE(m_client->vehicle, 0u);
        QCOMPARE(m_client->motion.position, exit);

        // The boat and the pig came back with their chunk; the pig touches the boat and sits again.
        ticks(2);
        QCOMPARE(m_server->entities().boats().all().size(), 1);
        const QcBoat &back = m_server->entities().boats().all().constFirst();
        QVERIFY(back.passenger != 0);
        const QcCreature *sitting = m_server->entities().creatures().find(back.passenger);
        QVERIFY(sitting);
        QCOMPARE(sitting->vehicle, back.id);
        QCOMPARE(sitting->pose, QcPose::Sitting);
    }

    void placingBeyondTheLimitIsRefused()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        digPond();
        m_server->entities().boats().setLimit(1);
        QVERIFY(spawnBoat({m_feet.x + 10.5, floatingHeight(), m_feet.z + 0.5}, 0.0f));
        chat(u"/give oak_boat"_s);
        ticks(2);
        input(0.0f, 0.0f);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:oak_boat"))});
        send(QcMsg::UseItem{m_feet.offset(3, -1, 0), QcBlockFace::Up});
        QCOMPARE(m_server->entities().boats().all().size(), 1);
        QCOMPARE(countOf(item("qtcraft:oak_boat")), 1);

        // With room again it is placed.
        m_server->entities().boats().setLimit(2);
        send(QcMsg::UseItem{m_feet.offset(3, -1, 0), QcBlockFace::Up});
        QCOMPARE(m_server->entities().boats().all().size(), 2);
        QCOMPARE(countOf(item("qtcraft:oak_boat")), 0);
    }

    void aPassengerWhoseBoatLeftWithItsChunkStandsUp()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        const int border = chunkBorderOnFloor();
        const Seated seated = pigAcrossTheBorder(border);
        QCOMPARE(boat(seated.boat).passenger, seated.pig);
        const QcChunkPos boatChunk = QcPhysics::blockContaining(boat(seated.boat).motion.position).chunk();
        const QcCreature *pig = m_server->entities().creatures().find(seated.pig);
        QVERIFY(QcPhysics::blockContaining(pig->motion.position).chunk() != boatChunk);
        QCOMPARE(pig->pose, QcPose::Sitting);

        // The boat's chunk leaves the world; the pig in the other chunk stays and stands up.
        const QHash<QcChunkPos, QcEntityRecord> records = QcEntityRecords::take(m_server->entities(), {boatChunk});
        QVERIFY(records.contains(boatChunk));
        QVERIFY(m_server->entities().boats().all().isEmpty());
        pig = m_server->entities().creatures().find(seated.pig);
        QVERIFY(pig);
        QCOMPARE(pig->vehicle, 0u);
        QCOMPARE(pig->pose, QcPose::Standing);

        // A boat removed any other way: the tick lets its passenger out.
        QVERIFY(m_server->entities().creatures().remove(seated.pig));
        const Seated again = pigAcrossTheBorder(border);
        QCOMPARE(boat(again.boat).passenger, again.pig);
        m_server->entities().boats().editableAll().clear();
        ticks(1);
        pig = m_server->entities().creatures().find(again.pig);
        QCOMPARE(pig->vehicle, 0u);
        QCOMPARE(pig->pose, QcPose::Standing);
        QVERIFY(entityProblems().isEmpty());
    }

    void boatsComeToRestAndWakeWhenSomethingChanges()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        digPond();
        QcBoat *first = spawnBoat({m_feet.x + 5.5, floatingHeight() + 0.3, m_feet.z + 0.5}, 0.0f);
        QVERIFY(first);
        first->motion.velocity = {1.0, 0.0, 0.0};
        first->spin = 3.0f;
        const quint32 a = first->id;
        const QcBoat *second = spawnBoat({m_feet.x + 10.5, floatingHeight(), m_feet.z + 0.5}, 0.0f);
        QVERIFY(second);
        const quint32 b = second->id;
        ticks(1);
        QCOMPARE(m_server->entities().boats().lastStepped(), 2);

        // Left alone, both come to rest floating and are not stepped any more.
        ticks(200);
        QCOMPARE(m_server->entities().boats().lastStepped(), 0);
        QVERIFY(boat(a).rest && boat(b).rest);
        QCOMPARE(boat(a).motion.velocity, QcVec3{});
        QVERIFY(boat(a).motion.inWater);
        QVERIFY(std::abs(boat(a).motion.position.y - floatingHeight()) < 1e-3);

        // A block of their chunks changes elsewhere: they read their blocks again and keep resting.
        m_server->world().setBlock(m_feet.offset(16, 0, 4), id("qtcraft:stone"));
        ticks(1);
        QCOMPARE(m_server->entities().boats().lastStepped(), 0);

        // The water under the first one drains away: it falls.
        const QcVec3 floating = boat(a).motion.position;
        const QcBlockPos under = QcPhysics::blockContaining(floating);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                m_server->world().setBlock(under.offset(dx, 0, dz), qcAirBlockId);
                m_server->world().setBlock(under.offset(dx, -1, dz), qcAirBlockId);
            }
        }
        ticks(1);
        QCOMPARE(m_server->entities().boats().lastStepped(), 1);
        ticks(5);
        QVERIFY(boat(a).motion.position.y < floating.y - 0.1);

        // A new boat put half into the second one pushes it away.
        const QcVec3 rested = boat(b).motion.position;
        QVERIFY(spawnBoat(rested + QcVec3{0.6, 0.0, 0.0}, 0.0f));
        ticks(3);
        QVERIFY(boat(b).motion.position.x < rested.x - 0.01);
    }
};

QTEST_GUILESS_MAIN(TestBoatRiders)
#include "test_boat_riders.moc"
