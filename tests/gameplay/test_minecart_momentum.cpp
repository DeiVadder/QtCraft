#include "common/protocol/QcInProcessTransport.h"
#include "common/world/QcMinecartBody.h"
#include "QcRailFixture.h"
#include "server/core/QcServerClient.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

#include <functional>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// The server's world with the chunks of some columns taken away (not loaded).
class QcCutWorld : public QcBlockAccess
{
public:
    QcCutWorld(const QcBlockAccess &world, std::function<bool(QcBlockPos)> cut)
        : m_world(world)
        , m_cut(std::move(cut))
    {}

    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override { return m_cut(pos) ? std::nullopt : m_world.blockAt(pos); }
    const QcContentRegistry &content() const override { return m_world.content(); }

private:
    const QcBlockAccess &m_world;
    std::function<bool(QcBlockPos)> m_cut;
};

// Minecarts with momentum off the rails (decision E-430), ticked by hand and the same in all four
// rotations: a cart reaching the open end of a track flies on with its speed and falls freely, off
// the top of a ramp it flies up, off a curve along the side of its end, off a powered rail with the
// boost; it lands and stops soon on stone, lands on a track below and rolls on along it, lands across
// a track and stands there until a cart bumps it on; two carts meeting in the air bounce off each
// other; a rider stays seated and takes no fall damage; off the rails it coasts far on ice and blue
// ice and stops within a few ticks on stone and on fences, rests without being stepped and waits with
// its speed before a chunk that is not loaded; a cart saved in flight flies on after a restart as if
// nothing had happened.
class TestMinecartMomentum : public QcRailFixture
{
    Q_OBJECT

private:
    // A cart off the rails at the local point (x, y, z) with the local velocity (dx, dz), in the air
    // or on the ground (a rail far off carries it into the world and goes again).
    quint32 looseCart(const QcVec3 &where, double dx, double dz, bool onGround)
    {
        lay({{0, 0, 13}});
        const quint32 id = cartAt(0, 0, 13);
        put(0, 0, 13, u"air"_s);
        QcMinecart &loose = *m_server->carts().find(id);
        loose.motion.position = point(where.x, where.y, where.z);
        loose.motion.velocity = direction(dx, dz);
        loose.motion.onGround = onGround;
        loose.rest.reset();
        return id;
    }

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // Where the cart is, in the area's coordinates, after each of `count` ticks.
    QList<QcVec3> trace(quint32 id, int count)
    {
        QList<QcVec3> points;
        for (int tick = 0; tick < count; ++tick) {
            ticks(1);
            points.append(local(cart(id).motion.position));
        }
        return points;
    }

    bool onRails(quint32 id) { return QcMinecartMotion::segmentAt(cart(id).motion.position, m_server->carts().rails(), m_server->world()).has_value(); }

    // Takes the floor away from x `fromX` on (rows z 4 to 10) and lays a lower one at y `floorY`.
    void dropFloor(int fromX, int floorY)
    {
        for (int x = fromX; x < areaSize; ++x) {
            for (int z = 4; z <= 10; ++z) {
                for (int y = floorY + 1; y <= -1; ++y) {
                    put(x, y, z, u"air"_s);
                }
                put(x, floorY, z, u"stone"_s);
            }
        }
    }

    // Distance a cart off the rails covers in 100 ticks from `speed` (local x) along row `z`, on the
    // ground, after the row's floor was made of `name`.
    double coasted(int z, const QString &name, double speed)
    {
        for (int x = 0; x < areaSize; ++x) {
            put(x, -1, z, name);
        }
        lay({{1, 0, z - 1}});
        const quint32 id = cartAt(1, 0, z - 1);
        QcMinecart &moving = *m_server->carts().find(id);
        moving.motion.position = point(1.5, 0.0, z + 0.5);
        moving.motion.velocity = direction(speed, 0.0);
        moving.motion.onGround = true;
        ticks(100);
        return local(cart(id).motion.position).x - 1.5;
    }

    void connectPlayer()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        pump();
        ticks(2);
    }

private slots:
    void aCartFliesOffTheEndOfATrackAndLandsBelow()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            lay({{1, 0, 7}, {2, 0, 7}, {3, 0, 7}, {4, 0, 7}, {5, 0, 7}, {6, 0, 7}});
            dropFloor(7, -4);
            const quint32 id = cartAt(2, 0, 7, 8.0, 0.0);
            const QString turn = u"turns %1"_s.arg(m_turns);

            // It rolls to the end of the track without stopping there.
            int left = -1;
            double speedOnTrack = 0.0;
            QList<QcVec3> path;
            for (int tick = 0; tick < 80; ++tick) {
                const bool before = onRails(id);
                const double speed = cart(id).motion.velocity.length();
                ticks(1);
                path.append(local(cart(id).motion.position));
                if (before && !onRails(id) && left < 0) {
                    left = tick;
                    speedOnTrack = speed;
                }
            }
            QVERIFY2(left > 0, qPrintable(turn));
            QVERIFY2(path[left].x > 7.0, qPrintable(u"%1: %2"_s.arg(turn).arg(path[left].x)));

            // Off the track it goes on in a curve: forwards all the way, never up, faster down.
            for (qsizetype i = left + 1; i < path.size(); ++i) {
                QVERIFY2(path[i].x >= path[i - 1].x - 1e-9, qPrintable(u"%1, tick %2"_s.arg(turn).arg(i)));
                QVERIFY2(path[i].y <= path[i - 1].y + 1e-9, qPrintable(u"%1, tick %2"_s.arg(turn).arg(i)));
                QVERIFY2(std::abs(path[i].z - 7.5) < 1e-9, qPrintable(turn));
            }

            // It landed on the floor three blocks lower, well beyond the edge, and stands.
            const QcVec3 landed = path.constLast();
            QVERIFY2(std::abs(landed.y - -3.0) < 1e-9, qPrintable(u"%1: %2"_s.arg(turn).arg(landed.y)));
            QVERIFY2(landed.x > 8.5 && landed.x < 13.0, qPrintable(u"%1: %2"_s.arg(turn).arg(landed.x)));
            QCOMPARE(cart(id).motion.velocity.length(), 0.0);
            QVERIFY2(speedOnTrack > 3.0, qPrintable(u"%1: %2"_s.arg(turn).arg(speedOnTrack)));
            QVERIFY(entityProblems().isEmpty());
        }
    }

    void aBlockAtTheEndOfATrackStillStopsTheCart()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            lay({{1, 0, 7}, {2, 0, 7}, {3, 0, 7}, {4, 0, 7}});
            // A slab is enough: the end of the track is blocked.
            put(5, 0, 7, u"stone_slab"_s);
            const quint32 id = cartAt(2, 0, 7, 6.0, 0.0);
            ticks(60);
            QVERIFY(onRails(id));
            const QcVec3 stopped = local(cart(id).motion.position);
            QVERIFY2(stopped.x > 4.9 && stopped.x < 5.0, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(stopped.x)));
            QCOMPARE(cart(id).motion.velocity.length(), 0.0);
        }
    }

    void offTheTopOfARampTheCartFliesUp()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            lay({{1, 0, 7}, {2, 0, 7}, {3, 0, 7}, {4, 0, 7}});
            // The ramp rises to the east; nothing lies at its top.
            put(5, 0, 7, u"rail[facing=east,shape=ascending]"_s);
            QCOMPARE(ends(5, 0, 7), u"east+,west"_s);
            const quint32 id = cartAt(2, 0, 7, 8.0, 0.0);
            const QList<QcVec3> path = trace(id, 60);

            // It leaves the ramp's top a metre up and rises higher before it falls (an empty cart has lost
            // speed to drag by then).
            double peak = 0.0;
            for (const QcVec3 &point : path) {
                peak = std::max(peak, point.y);
            }
            QVERIFY2(peak > 1.1, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(peak)));
            QVERIFY2(std::abs(path.constLast().y) < 1e-9, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(path.constLast().y)));
            QVERIFY2(path.constLast().x > 7.0, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(path.constLast().x)));
            QVERIFY(!onRails(id));
        }
    }

    void aCartLandingOnATrackBelowRollsOn()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // A track on a wall one block high, ending in the air; one cell on, a track on the floor.
            for (int x = 1; x <= 5; ++x) {
                put(x, 0, 7, u"stone"_s);
            }
            lay({{1, 1, 7}, {2, 1, 7}, {3, 1, 7}, {4, 1, 7}, {5, 1, 7}});
            lay({{7, 0, 7}, {8, 0, 7}, {9, 0, 7}, {10, 0, 7}, {11, 0, 7}, {12, 0, 7}, {13, 0, 7}});
            put(14, 0, 7, u"stone"_s);
            QCOMPARE(ends(5, 1, 7), u"east,west"_s);
            QCOMPARE(ends(7, 0, 7), u"east,west"_s);
            const quint32 id = cartAt(2, 1, 7, 8.0, 0.0);

            // It flies, lands on the lower track and rolls on along it.
            bool flew = false;
            bool landed = false;
            for (int tick = 0; tick < 40 && !landed; ++tick) {
                ticks(1);
                flew = flew || !onRails(id);
                landed = flew && onRails(id);
            }
            QVERIFY2(landed, qPrintable(u"turns %1"_s.arg(m_turns)));
            // Close enough above the track, the next step sets it on it.
            ticks(1);
            const QcVec3 down = local(cart(id).motion.position);
            QVERIFY2(down.x > 7.0 && down.x < 10.0 && std::abs(down.y) < 1e-9, qPrintable(u"turns %1: %2 %3"_s.arg(m_turns).arg(down.x).arg(down.y)));
            QVERIFY2(cart(id).motion.velocity.length() > 3.0, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(cart(id).motion.velocity.length())));
            ticks(60);
            QVERIFY2(local(cart(id).motion.position).x > down.x + 2.0, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(local(cart(id).motion.position).x)));
            QVERIFY(onRails(id));
        }
    }

    void offTheRailsACartCoastsFarOnIceAndStopsSoonOnStone()
    {
        // From the original game's slipperiness: the cart keeps 60 % per tick on stone, 98 % on ice
        // and 98.9 % on blue ice, each tick before it moves, and stands below the rest speed.
        const auto expected = [](double speed, double keep) {
            double covered = 0.0;
            for (int tick = 0; tick < 100; ++tick) {
                speed = speed < QcMinecartMotion::restSpeed ? 0.0 : speed * keep;
                covered += speed * QcPhysics::tickDuration;
            }
            return covered;
        };
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            const QString turn = u"turns %1"_s.arg(m_turns);
            buildArea();
            const double onStone = coasted(3, u"stone"_s, 6.0);
            QVERIFY2(std::abs(onStone - expected(6.0, 0.6)) < 1e-9, qPrintable(u"%1: %2"_s.arg(turn).arg(onStone)));
            QVERIFY(onStone < 0.5);
            const double onIce = coasted(7, u"ice"_s, 4.0);
            QVERIFY2(std::abs(onIce - expected(4.0, 0.98)) < 1e-9, qPrintable(u"%1: %2"_s.arg(turn).arg(onIce)));
            QVERIFY(onIce > 8.0);
            const double onBlueIce = coasted(11, u"blue_ice"_s, 3.0);
            QVERIFY2(std::abs(onBlueIce - expected(3.0, 0.989)) < 1e-9, qPrintable(u"%1: %2"_s.arg(turn).arg(onBlueIce)));
        }
    }

    void aCartSavedInFlightFliesOnAfterARestart()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        buildArea(config);
        QList<QcBlockPos> line;
        for (int x = 4; x < areaSize; ++x) {
            line.append({x, 0, 7});
        }
        lay(line);
        // Off the edge of the area the cart falls far.
        for (int z = 4; z <= 10; ++z) {
            put(areaSize, -1, z, u"air"_s);
        }
        const quint32 id = cartAt(9, 0, 7, 6.0, 0.0);
        for (int tick = 0; tick < 60 && (onRails(id) || cart(id).motion.velocity.y > -1.0); ++tick) {
            ticks(1);
        }
        QVERIFY(!onRails(id));
        const QcMinecart flying = cart(id);
        QVERIFY(flying.motion.velocity.y < -1.0);
        QVERIFY(flying.motion.velocity.x != 0.0 || flying.motion.velocity.z != 0.0);

        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = m_viewDistance;
        config.keepChunksLoaded = true;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        loadAround(QcPhysics::blockContaining(flying.motion.position));

        QCOMPARE(m_server->carts().all().size(), 1);
        QcMinecart expected = m_server->carts().all().constFirst();
        QCOMPARE(expected.motion.position, flying.motion.position);
        QCOMPARE(expected.motion.velocity, flying.motion.velocity);

        // It goes on as the motion alone says: in the air, falling faster (two ticks while the
        // player joins, five more).
        const quint32 again = expected.id;
        for (int tick = 0; tick < 7; ++tick) {
            QcMinecartMotion::step(expected, m_server->carts().rails(), m_server->world(), QcPhysics::tickDuration);
        }
        connectPlayer();
        ticks(5);
        QCOMPARE(cart(again).motion.position, expected.motion.position);
        QVERIFY(cart(again).motion.velocity.y < flying.motion.velocity.y);
    }

    void aCartLeavesACurveAlongTheSideOfItsEnd()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            const QString turn = u"turns %1"_s.arg(m_turns);
            buildArea();
            lay({{1, 0, 4}, {2, 0, 4}, {3, 0, 4}, {4, 0, 4}});
            // The last rail turns from the west to the south; nothing lies beyond its south end.
            put(5, 0, 4, u"rail[facing=south,shape=curve]"_s);
            QCOMPARE(ends(5, 0, 4), u"south,west"_s);
            const quint32 id = cartAt(2, 0, 4, 6.0, 0.0);
            const QList<QcVec3> path = trace(id, 30);
            qsizetype left = -1;
            for (qsizetype i = 0; i < path.size() && left < 0; ++i) {
                left = path[i].z > 5.0 ? i : -1;
            }
            QVERIFY2(left > 0, qPrintable(turn));

            // Off the curve it goes straight south out of the end, not along the curve's diagonal.
            for (qsizetype i = left; i < path.size(); ++i) {
                QVERIFY2(std::abs(path[i].x - 5.5) < 1e-9, qPrintable(u"%1, tick %2: %3"_s.arg(turn).arg(i).arg(path[i].x)));
            }
            QVERIFY2(path.constLast().z > 5.2, qPrintable(u"%1: %2"_s.arg(turn).arg(path.constLast().z)));
            QVERIFY(!onRails(id));
            // It looks the way it flew.
            QVERIFY2(std::abs(std::remainder(double(cart(id).yaw) - double(QcMinecartMotion::yawAlong(direction(0.0, 1.0))), 180.0)) < 1e-3,
                     qPrintable(u"%1: %2"_s.arg(turn).arg(cart(id).yaw)));
        }
    }

    void offAPoweredRailTheCartFliesWithTheBoost()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            const QString turn = u"turns %1"_s.arg(m_turns);
            buildArea();
            lay({{1, 0, 7}, {2, 0, 7}, {3, 0, 7}});
            lay({{4, 0, 7}, {5, 0, 7}}, u"powered_rail"_s);
            put(5, 0, 8, u"lever[attach=floor]"_s);
            flip(5, 0, 8);
            // An activator rail last on a second line, powered by its own lever.
            lay({{1, 0, 2}, {2, 0, 2}, {3, 0, 2}});
            lay({{4, 0, 2}}, u"activator_rail"_s);
            put(4, 0, 1, u"lever[attach=floor]"_s);
            flip(4, 0, 1);
            dropFloor(6, -4);
            for (int x = 5; x < areaSize; ++x) {
                put(x, -1, 2, u"air"_s);
            }
            ticks(2);
            QCOMPARE(property(5, 0, 7, u"powered"_s), u"true"_s);
            const quint32 id = cartAt(2, 0, 7, 3.0, 0.0);
            const quint32 hopper = cartOf(u"qtcraft:hopper_minecart"_s, 2, 0, 2);
            m_server->carts().find(hopper)->motion.velocity = direction(3.0, 0.0);

            // Off the powered rail it flies faster than it came, and lands far beyond the edge.
            double fastest = 0.0;
            for (int tick = 0; tick < 60; ++tick) {
                ticks(1);
                if (!onRails(id)) {
                    const QcVec3 velocity = cart(id).motion.velocity;
                    fastest = std::max(fastest, std::hypot(velocity.x, velocity.z));
                }
            }
            QVERIFY2(fastest > 4.0, qPrintable(u"%1: %2"_s.arg(turn).arg(fastest)));
            QVERIFY2(local(cart(id).motion.position).x > 8.0, qPrintable(u"%1: %2"_s.arg(turn).arg(local(cart(id).motion.position).x)));
            // The hopper cart passed the activator rail last and left it: it stays off.
            QVERIFY2(!onRails(hopper) && local(cart(hopper).motion.position).x > 5.0, qPrintable(turn));
            QVERIFY2(!cart(hopper).enabled, qPrintable(turn));
            QVERIFY(entityProblems().isEmpty());
        }
    }

    void aRiderInFlightStaysSeatedAndTakesNoFallDamage()
    {
        buildArea();
        connectPlayer();
        lay({{1, 0, 7}, {2, 0, 7}, {3, 0, 7}, {4, 0, 7}, {5, 0, 7}, {6, 0, 7}});
        dropFloor(7, -6);
        const quint32 id = cartAt(2, 0, 7);
        player().motion.position = point(2.5, 0.0, 8.5);
        QVERIFY(m_server->systems().minecarts.use(player(), id));
        m_server->carts().find(id)->motion.velocity = direction(8.0, 0.0);
        const double health = player().health;

        // A fall of five blocks: it flies, lands and stands; the rider sits in it all the way.
        bool flew = false;
        for (int tick = 0; tick < 80; ++tick) {
            ticks(1);
            flew = flew || !onRails(id);
            QCOMPARE(cart(id).rider, player().id);
            QVERIFY2((player().motion.position - cart(id).seat).length() < 1e-9, qPrintable(QString::number(tick)));
        }
        QVERIFY(flew);
        QVERIFY(std::abs(local(cart(id).motion.position).y - -5.0) < 1e-9);
        QCOMPARE(player().health, health);
        QVERIFY(entityProblems().isEmpty());
    }

    void twoCartsMeetingInTheAirBounceOffEachOther()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            const QString turn = u"turns %1"_s.arg(m_turns);
            buildArea();
            // Two tracks facing each other across a pit three blocks deep and five wide.
            lay({{1, 0, 7}, {2, 0, 7}, {3, 0, 7}, {4, 0, 7}});
            lay({{10, 0, 7}, {11, 0, 7}, {12, 0, 7}});
            put(13, 0, 7, u"stone"_s);
            for (int x = 5; x <= 9; ++x) {
                for (int z = 4; z <= 10; ++z) {
                    put(x, -1, z, u"air"_s);
                    put(x, -2, z, u"air"_s);
                    put(x, -3, z, u"air"_s);
                    put(x, -4, z, u"stone"_s);
                }
            }
            const quint32 west = cartAt(2, 0, 7, 6.0, 0.0);
            const quint32 east = cartAt(12, 0, 7, -6.0, 0.0);

            // They meet in the air above the pit and never pass through each other.
            bool metInTheAir = false;
            for (int tick = 0; tick < 80; ++tick) {
                ticks(1);
                const QcVec3 a = local(cart(west).motion.position);
                const QcVec3 b = local(cart(east).motion.position);
                QVERIFY2(a.x < b.x, qPrintable(u"%1, tick %2"_s.arg(turn).arg(tick)));
                const bool close = b.x - a.x < QcMinecartBody::width + 0.05 && std::abs(a.y - b.y) < QcMinecartBody::height;
                metInTheAir = metInTheAir || (close && a.y < -0.1 && a.y > -2.9);
            }
            QVERIFY2(metInTheAir, qPrintable(turn));

            // Both lie at the bottom of the pit, side by side, and stand.
            const QcVec3 a = local(cart(west).motion.position);
            const QcVec3 b = local(cart(east).motion.position);
            QVERIFY2(std::abs(a.y - -3.0) < 1e-9 && std::abs(b.y - -3.0) < 1e-9, qPrintable(u"%1: %2 %3"_s.arg(turn).arg(a.y).arg(b.y)));
            QVERIFY2(b.x - a.x >= QcMinecartBody::width - 1e-6, qPrintable(u"%1: %2 %3"_s.arg(turn).arg(a.x).arg(b.x)));
            QVERIFY2(a.x > 5.0 && b.x < 10.0, qPrintable(u"%1: %2 %3"_s.arg(turn).arg(a.x).arg(b.x)));
            QCOMPARE(cart(west).motion.velocity.length(), 0.0);
            QCOMPARE(cart(east).motion.velocity.length(), 0.0);
            QVERIFY(entityProblems().isEmpty());
        }
    }

    void aCartLandingAcrossATrackStandsUntilACartBumpsItOn()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            const QString turn = u"turns %1"_s.arg(m_turns);
            buildArea();
            // A track on a wall one block high ends in the air above a track running north-south.
            for (int x = 1; x <= 5; ++x) {
                put(x, 0, 7, u"stone"_s);
            }
            lay({{1, 1, 7}, {2, 1, 7}, {3, 1, 7}, {4, 1, 7}, {5, 1, 7}});
            QList<QcBlockPos> crossing;
            for (int z = 1; z <= 12; ++z) {
                crossing.append({7, 0, z});
            }
            lay(crossing);
            const quint32 id = cartAt(3, 1, 7, 8.0, 0.0);
            bool flew = false;
            bool landed = false;
            for (int tick = 0; tick < 40 && !landed; ++tick) {
                ticks(1);
                flew = flew || !onRails(id);
                landed = flew && onRails(id);
            }
            QVERIFY2(landed, qPrintable(turn));

            // It got on the crossing track without speed along it and stands there, like in the
            // original game.
            ticks(3);
            const QcVec3 standing = local(cart(id).motion.position);
            QVERIFY2(std::abs(standing.x - 7.5) < 1e-9 && std::abs(standing.y) < 1e-9, qPrintable(u"%1: %2"_s.arg(turn).arg(standing.x)));
            QCOMPARE(cart(id).motion.velocity.length(), 0.0);

            // It does not block the line: a cart rolling along it bumps it on.
            const quint32 bumper = cartAt(7, 0, 2, 0.0, 6.0);
            ticks(40);
            QVERIFY2(local(cart(id).motion.position).z > standing.z + 1.0, qPrintable(u"%1: %2"_s.arg(turn).arg(local(cart(id).motion.position).z)));
            QVERIFY(onRails(id));
            QVERIFY(onRails(bumper));
            QVERIFY(entityProblems().isEmpty());
        }
    }

    void aSlowCartFallsOffTheEndWithoutGettingBackOnIt()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            lay({{1, 0, 7}, {2, 0, 7}, {3, 0, 7}, {4, 0, 7}, {5, 0, 7}, {6, 0, 7}});
            dropFloor(7, -4);
            // Slow, it is still close to the end just below it: in the air it gets on a track only from
            // above or level with it, so it falls.
            const quint32 id = cartAt(5, 0, 7, 2.5, 0.0);
            const QList<QcVec3> path = trace(id, 60);
            QVERIFY2(std::abs(path.constLast().y - -3.0) < 1e-9, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(path.constLast().y)));
            QVERIFY(!onRails(id));
        }
    }

    void offTheRailsOnAFenceTheCartStopsSoon()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            for (int x = 3; x <= 12; ++x) {
                put(x, 0, 7, u"oak_fence[north=true,east=true,south=true,west=true]"_s);
            }
            // On the fences (1.5 high) it grips like on stone: 60 % of its speed per tick.
            const quint32 id = looseCart({4.5, 1.5, 7.5}, 3.0, 0.0, true);
            ticks(20);
            const QcVec3 stopped = local(cart(id).motion.position);
            QVERIFY2(std::abs(stopped.y - 1.5) < 1e-9, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(stopped.y)));
            QVERIFY2(stopped.x < 4.5 + 0.3, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(stopped.x)));
            QCOMPARE(cart(id).motion.velocity.length(), 0.0);
            QVERIFY(cart(id).rest.has_value());
        }
    }

    void aCartOffTheRailsRestsUntilSomethingChanges()
    {
        buildArea();
        const quint32 id = looseCart({6.5, 0.0, 6.5}, 0.0, 0.0, true);
        ticks(2);
        QVERIFY(cart(id).rest.has_value());
        QVERIFY(cart(id).rest->offRails);

        // A push wakes it and it slides a little; it rests again.
        m_server->carts().find(id)->motion.velocity = direction(2.0, 0.0);
        ticks(1);
        QVERIFY(local(cart(id).motion.position).x > 6.5);
        ticks(10);
        QVERIFY(cart(id).rest.has_value());

        // Taking the ground from under it wakes it: it falls.
        const QcVec3 now = local(cart(id).motion.position);
        for (int x = int(std::floor(now.x)) - 1; x <= int(std::floor(now.x)) + 1; ++x) {
            for (int z = int(std::floor(now.z)) - 1; z <= int(std::floor(now.z)) + 1; ++z) {
                put(x, -1, z, u"air"_s);
                put(x, -2, z, u"air"_s);
            }
        }
        ticks(1);
        QVERIFY(!cart(id).rest.has_value());
        ticks(20);
        QVERIFY2(local(cart(id).motion.position).y < -0.5, qPrintable(QString::number(local(cart(id).motion.position).y)));

        // What resting saves: 144 carts standing off the rails, apart, ticked at rest and stepped
        // (debug build, relative only).
        buildArea();
        QList<QcMinecart> stepped;
        for (int i = 0; i < 144; ++i) {
            const int column = i % 12;
            const int row = i / 12;
            const quint32 standing = looseCart({1.5 + column, 0.0, 0.5 + row}, 0.0, 0.0, true);
            stepped.append(cart(standing));
        }
        ticks(2);
        QElapsedTimer timer;
        timer.start();
        for (int tick = 0; tick < 50; ++tick) {
            m_server->carts().tick(m_server->world(), {}, QcPhysics::tickDuration);
        }
        const qint64 resting50 = timer.nsecsElapsed();
        timer.restart();
        for (int tick = 0; tick < 50; ++tick) {
            for (QcMinecart &copy : stepped) {
                QcMinecartMotion::step(copy, m_server->carts().rails(), m_server->world(), QcPhysics::tickDuration);
            }
        }
        const qint64 stepping50 = timer.nsecsElapsed();
        qInfo("144 carts off the rails, 50 ticks: at rest %.2f ms, stepped %.2f ms", double(resting50) / 1e6, double(stepping50) / 1e6);
    }

    void aCartWaitsWithItsSpeedBeforeAChunkThatIsNotLoaded()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            const QcCutWorld cut(m_server->world(), [this](QcBlockPos pos) { return local(QcVec3{pos.x + 0.5, 0.0, pos.z + 0.5}).x >= 9.0; });
            for (const bool inTheAir : {true, false}) {
                QcMinecart moving = cart(looseCart({8.4, inTheAir ? 2.0 : 0.0, 3.5 + (inTheAir ? 0.0 : 4.0)}, 5.0, 0.0, !inTheAir));
                const QcMinecart before = moving;
                const QcMinecartMotion::Result waited = QcMinecartMotion::step(moving, m_server->carts().rails(), cut, QcPhysics::tickDuration);
                QVERIFY(waited.waiting);
                QCOMPARE(moving.motion.position, before.motion.position);
                QCOMPARE(moving.motion.velocity, before.motion.velocity);

                // Loaded, it goes on with that speed.
                QcMinecartMotion::step(moving, m_server->carts().rails(), m_server->world(), QcPhysics::tickDuration);
                QVERIFY2(local(moving.motion.position).x > 8.5, qPrintable(u"turns %1"_s.arg(m_turns)));
            }
        }
    }

    void aCoastingCartKnowsItIsOnTheGroundAfterLoading()
    {
        buildArea();
        for (int x = 0; x < areaSize; ++x) {
            put(x, -1, 7, u"ice"_s);
        }
        const quint32 id = looseCart({2.5, 0.0, 7.5}, 4.0, 0.0, true);
        ticks(3);
        // onGround is not saved: a loaded cart has it false and goes on the same.
        QcMinecart going = cart(id);
        QcMinecart loaded = going;
        loaded.motion.onGround = false;
        for (int tick = 0; tick < 5; ++tick) {
            QcMinecartMotion::step(going, m_server->carts().rails(), m_server->world(), QcPhysics::tickDuration);
            QcMinecartMotion::step(loaded, m_server->carts().rails(), m_server->world(), QcPhysics::tickDuration);
        }
        QCOMPARE(loaded.motion.position, going.motion.position);
        QCOMPARE(loaded.motion.velocity, going.motion.velocity);
        QVERIFY(loaded.motion.onGround);
    }
};

QTEST_GUILESS_MAIN(TestMinecartMomentum)
#include "test_minecart_momentum.moc"
