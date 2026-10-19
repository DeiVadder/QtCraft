#include "common/world/QcMinecartBody.h"
#include "QcRailFixture.h"
#include "QcTestUtil.h"
#include "server/minecarts/QcServerMinecarts.h"

#include <QTemporaryDir>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Riders of minecarts on a running server (decision E-383): a rider who leaves the game or is saved
// while riding comes back standing beside the cart, and the forward key pushes the cart only along
// its track, never faster than the push's top speed, not at all off the rails, and never into a
// cart that cannot move on; a piston pushes a ridden cart and its rider stays in.
class TestMinecartRiders : public QcRailFixture
{
    Q_OBJECT

private slots:
    void aRiderIsSavedBesideTheCartOnDisconnectAndOnStop()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/gamerule mobSpawning false"_s);
        const quint32 id = railsBesidePlayer();

        // Riding, then the connection closes: back in the game he stands on the floor beside the
        // track's level, not inside the block under the rail, and stays there.
        ride(id);
        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);
        QCOMPARE(cart(id).rider, 0u);
        m_server->waitForStorage();
        rejoin();
        expectStandingFree();

        // Riding while the server stops: the same after the restart.
        ride(id);
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = m_viewDistance;
        config.keepChunksLoaded = true;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        rejoin();
        expectStandingFree();
    }

    void theForwardKeyPushesAlongTheTrackUpToItsTopSpeed()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        m_origin = m_feet.offset(1, 0, 0);
        m_turns = 0;
        tunnel(20);
        const quint32 id = cartAt(1, 0, 0);
        ride(id);

        // Looking along the track it reaches the top speed and no more. Looking 80 degrees off the
        // track the push is too weak to start the cart (less than the speed at which it stands
        // still), but it keeps a rolling cart rolling up to the top speed, and no faster (the speed
        // counts along the track, not along the view).
        for (const float yaw : {90.0f, 10.0f}) {
            m_server->carts().find(id)->motion.velocity = direction(yaw == 90.0f ? 0.0 : 3.0, 0.0);
            double fastest = 0.0;
            for (int tick = 0; tick < 800; ++tick) {
                steer(yaw, 1.0f);
                fastest = std::max(fastest, cart(id).motion.velocity.length());
                keepOnTrack(id);
            }
            QVERIFY2(fastest <= QcServerMinecarts::riderPushTopSpeed + 1e-9, qPrintable(u"yaw %1: %2"_s.arg(yaw).arg(fastest)));
            QVERIFY2(fastest > QcServerMinecarts::riderPushTopSpeed - 0.1, qPrintable(u"yaw %1: %2"_s.arg(yaw).arg(fastest)));
            QCOMPARE(m_client->vehicle, id);
        }

        // Looking across the track it does not move at all.
        m_server->carts().find(id)->motion.velocity = {};
        for (int tick = 0; tick < 20; ++tick) {
            steer(0.0f, 1.0f);
        }
        QCOMPARE(cart(id).motion.velocity.length(), 0.0);
    }

    void theForwardKeyDoesNothingOffTheRails()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        // A cart standing on the grass of the stage, without rails.
        QcMinecart standing = m_server->carts().fromItem(u"qtcraft:minecart"_s);
        standing.motion.position = {m_feet.x + 2.5, double(m_feet.y), m_feet.z + 0.5};
        const quint32 id = m_server->systems().minecarts.spawn(standing)->id;
        ticks(2);
        const QcVec3 start = cart(id).motion.position;
        ride(id);
        for (int tick = 0; tick < 40; ++tick) {
            steer(90.0f, 1.0f);
        }
        QCOMPARE(m_client->vehicle, id);
        QVERIFY2((cart(id).motion.position - start).length() < 1e-9, qPrintable(QString::number((cart(id).motion.position - start).length())));

        // Nor while it falls: no steering through the air.
        m_server->carts().find(id)->motion.position = start + QcVec3{0.0, 3.0, 0.0};
        m_server->carts().find(id)->motion.onGround = false;
        for (int tick = 0; tick < 10; ++tick) {
            steer(90.0f, 1.0f);
        }
        QCOMPARE(m_client->vehicle, id);
        QVERIFY(cart(id).motion.position.y < start.y + 3.0);
        QCOMPARE(cart(id).motion.position.x, start.x);
        QCOMPARE(cart(id).motion.position.z, start.z);
    }

    void aRiderCannotPushHisCartIntoABlockedOne()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        m_origin = m_feet.offset(1, 0, 0);
        m_turns = 0;
        tunnel(4);
        // A cart at the dead end of the track, the rider's cart behind it, pushing towards it.
        const quint32 blocked = cartAt(4, 0, 0);
        const quint32 id = cartAt(2, 0, 0);
        ride(id);
        double closest = 2.0;
        for (int tick = 0; tick < 400; ++tick) {
            steer(90.0f, 1.0f);
            const QcVec3 between = cart(blocked).motion.position - cart(id).motion.position;
            closest = std::min(closest, std::hypot(between.x, between.z));
        }
        QVERIFY2(closest >= 0.9, qPrintable(QString::number(closest)));
        QVERIFY(local(cart(blocked).motion.position).x > 4.9);
        QCOMPARE(m_client->vehicle, id);
    }

    void aPistonPushesARiddenCartAndTheRiderStaysIn()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        // A lever, a piston facing east and rails in the row south of the player.
        m_origin = m_feet.offset(-1, 0, 1);
        m_turns = 0;
        for (int x = 0; x <= 8; ++x) {
            put(x, -1, 0, u"stone"_s);
            for (int y = 0; y <= 3; ++y) {
                put(x, y, 0, u"air"_s);
            }
        }
        put(0, 0, 0, u"lever[attach=floor]"_s);
        put(1, 0, 0, u"signal_piston[facing=east]"_s);
        lay({{2, 0, 0}, {3, 0, 0}, {4, 0, 0}, {5, 0, 0}, {6, 0, 0}});
        const quint32 id = cartAt(2, 0, 0);
        ride(id);

        flip(0, 0, 0);
        idle(5);
        // The cart was pushed out of the head's cell along its rails; its rider sits in it.
        QVERIFY(isBlock(2, 0, 0, "signal_piston_head"));
        QVERIFY2(local(cart(id).motion.position).x - QcMinecartBody::width / 2.0 >= 3.0 - 0.05, qPrintable(QString::number(local(cart(id).motion.position).x)));
        QCOMPARE(cart(id).rider, m_client->entityId);
        QCOMPARE(m_client->vehicle, id);
        const QcVec3 seat = cart(id).motion.position + QcVec3{0.0, QcMinecartBody::seatOffset, 0.0};
        QCOMPARE(cart(id).seat, seat);
        QVERIFY(entityProblems().isEmpty());
    }

private:
    // Rails from local x = 0 to `last` at z = 0 through a tunnel in the terrain beyond the stage, so
    // a rider has room for his head.
    void tunnel(int last)
    {
        QList<QcBlockPos> line;
        for (int x = 0; x <= last; ++x) {
            line.append({x, 0, 0});
            put(x, -1, 0, u"stone"_s);
            for (int y = 1; y <= 3; ++y) {
                put(x, y, 0, u"air"_s);
            }
        }
        lay(line);
    }

    // One tick of a rider's input: forward with the view along `yaw`.
    void steer(float yaw, float forward)
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.forward = forward;
        input.yaw = yaw;
        m_client->connection.send(QcMsg::PlayerInput{input});
        QcTestUtil::pump();
        ticks(1);
    }

    // A cart past the middle of the 21-rail line goes back ten rails with its speed (an endless
    // track). Its rider follows with the next tick: he is still where the cart last seated him.
    void keepOnTrack(quint32 id)
    {
        QcMinecart *moving = m_server->carts().find(id);
        if (local(moving->motion.position).x > 15.0) {
            moving->motion.position = moving->motion.position - direction(10.0, 0.0);
        }
    }

    // Three rails east of the player on his stage and a cart on the middle one; returns the cart.
    quint32 railsBesidePlayer()
    {
        m_origin = m_feet.offset(1, 0, 0);
        m_turns = 0;
        lay({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}});
        return cartAt(1, 0, 0);
    }

    void ride(quint32 id)
    {
        send(QcMsg::UseEntity{id});
        idle(2);
        QCOMPARE(m_client->vehicle, id);
    }

    void rejoin()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
        ticks(2);
    }

    void expectStandingFree()
    {
        QCOMPARE(m_client->vehicle, 0u);
        ticks(20);
        idle(1);
        const QcVec3 feet = m_client->motion.position;
        QVERIFY2(std::abs(feet.y - m_feet.y) < 1e-9, qPrintable(u"feet at %1, floor at %2"_s.arg(feet.y).arg(m_feet.y)));
        const QcBlockPos cell = QcPhysics::blockContaining(feet);
        QVERIFY(QcPhysics::collisionBoxesAt(m_server->world(), cell).isEmpty());
        QVERIFY(QcPhysics::collisionBoxesAt(m_server->world(), cell.offset(0, 1, 0)).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMinecartRiders)
#include "test_minecart_riders.moc"
