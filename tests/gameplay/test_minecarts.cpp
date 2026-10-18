#include "common/world/QcMinecartBody.h"
#include "QcRailFixture.h"
#include "server/minecarts/QcMinecartCollisions.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Rails and minecarts on a running server (stage 32.1, decision E-382): the shape a rail takes from
// the rails around it, the same in all four rotations; placing it; a cart rolling along straight
// track, curves and slopes, stopping at the end of a track before a block and trading speed with another cart, the
// same in all four rotations; a piston pushing a cart; a player placing, riding, steering, leaving
// and breaking a cart; and a cart that leaves the world with its chunk and comes back.
class TestMinecarts : public QcRailFixture
{
    Q_OBJECT

private slots:
    void railShapesInAllFourRotations()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // A line, a corner, a T, a slope up a block and a lone rail.
            lay({{1, 0, 1}, {2, 0, 1}, {3, 0, 1}});
            lay({{6, 0, 1}, {7, 0, 1}, {7, 0, 2}});
            lay({{1, 0, 5}, {2, 0, 5}, {3, 0, 5}, {2, 0, 6}});
            put(8, 0, 5, u"stone"_s);
            put(9, 0, 5, u"stone"_s);
            lay({{6, 0, 5}, {7, 0, 5}, {8, 1, 5}, {9, 1, 5}});
            lay({{12, 0, 12}});

            const QString turn = u" (turns %1)"_s.arg(m_turns);
            QCOMPARE(ends(2, 0, 1) + turn, u"east,west"_s + turn);
            QCOMPARE(ends(1, 0, 1) + turn, u"east,west"_s + turn);
            QCOMPARE(ends(7, 0, 1) + turn, u"south,west"_s + turn);
            QCOMPARE(ends(6, 0, 1) + turn, u"east,west"_s + turn);
            QCOMPARE(ends(7, 0, 2) + turn, u"north,south"_s + turn);
            // The branch of a T does not bend the bar.
            QCOMPARE(ends(2, 0, 5) + turn, u"east,west"_s + turn);
            QCOMPARE(ends(2, 0, 6) + turn, u"north,south"_s + turn);
            // The rail below the step rises towards it; the one on top lies flat.
            QCOMPARE(ends(7, 0, 5) + turn, u"east+,west"_s + turn);
            QCOMPARE(ends(8, 1, 5) + turn, u"east,west"_s + turn);
            QCOMPARE(ends(6, 0, 5) + turn, u"east,west"_s + turn);
            // A lone rail keeps its axis.
            QCOMPARE(ends(12, 0, 12) + turn, u"north,south"_s + turn);
        }
    }

    void railsJoinARailOneHigherOrLowerOnlyWhereItCanMeetThem()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // A line one block higher at z 3 passes beside the end of a lower line at z 2.
            for (int x = 3; x <= 9; ++x) {
                put(x, 0, 3, u"stone"_s);
            }
            lay({{3, 1, 3}, {4, 1, 3}, {5, 1, 3}, {6, 1, 3}, {7, 1, 3}, {8, 1, 3}, {9, 1, 3}});
            lay({{4, 0, 2}, {5, 0, 2}, {6, 0, 2}});
            // A rail one higher at the end of a line, alone: the line rises to it.
            put(6, 0, 9, u"stone"_s);
            lay({{6, 1, 9}});
            lay({{4, 0, 9}, {5, 0, 9}});
            // A line one block lower at z 6 passes beside the end of a line at z 5.
            for (int x = 9; x <= 12; ++x) {
                put(x, -1, 6, u"air"_s);
                put(x, -2, 6, u"stone"_s);
            }
            lay({{9, -1, 6}, {10, -1, 6}, {11, -1, 6}, {12, -1, 6}});
            lay({{8, 0, 5}, {9, 0, 5}, {10, 0, 5}});

            const QString turn = u" (turns %1)"_s.arg(m_turns);
            // The upper line runs on: the lower line ends straight and does not bend up to it.
            QCOMPARE(ends(6, 0, 2) + turn, u"east,west"_s + turn);
            QCOMPARE(ends(6, 1, 3) + turn, u"east,west"_s + turn);
            QCOMPARE(ends(5, 0, 9) + turn, u"east+,west"_s + turn);
            QCOMPARE(ends(10, 0, 5) + turn, u"east,west"_s + turn);
            QCOMPARE(ends(10, -1, 6) + turn, u"east,west"_s + turn);
        }
    }

    void placementFollowsTheViewAndNeedsGround()
    {
        buildArea();
        const QcBlockPos floor = at(4, -1, 4);
        for (const float yaw : {0.0f, 90.0f, 180.0f, 270.0f}) {
            const std::optional<QcBlockRules::Placement> placed =
                QcBlockRules::placement(m_server->world(), railState(), {floor, QcBlockFace::Up, {0.5, 1.0, 0.5}, yaw, 0.0f});
            QVERIFY(placed);
            const std::optional<QcRails::Track> track = QcRails::trackOf(*m_content, placed->state);
            const bool alongZ = track->ends[0].side == QcBlockFace::North || track->ends[0].side == QcBlockFace::South;
            QCOMPARE(alongZ, yaw == 0.0f || yaw == 180.0f);
        }

        // Next to a rail it joins the rail, wherever the player looks.
        lay({{4, 0, 4}});
        const std::optional<QcBlockRules::Placement> beside =
            QcBlockRules::placement(m_server->world(), railState(), {at(5, -1, 4), QcBlockFace::Up, {0.5, 1.0, 0.5}, 0.0f, 0.0f});
        QCOMPARE(QcRails::trackOf(*m_content, beside->state)->ends[0].side == QcBlockFace::East
                     || QcRails::trackOf(*m_content, beside->state)->ends[0].side == QcBlockFace::West,
                 true);

        // A pane has no solid top: no rail on it.
        put(8, 0, 8, u"glass_pane"_s);
        QVERIFY(!QcBlockRules::placement(m_server->world(), railState(), {at(8, 0, 8), QcBlockFace::Up, {0.5, 1.0, 0.5}, 0.0f, 0.0f}));
    }

    void railBreaksWithoutItsGround()
    {
        buildArea();
        lay({{4, 0, 4}});
        const QcBlockPos ground = at(4, -1, 4);
        chat(u"/setblock %1 %2 %3 air"_s.arg(ground.x).arg(ground.y).arg(ground.z));
        ticks(3);
        QCOMPARE(m_server->world().blockAt(at(4, 0, 4)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(droppedCount(item("qtcraft:rail")), 1);
    }

    void cartRollsAlongAStraightTrackAndLosesSpeed()
    {
        // Distance after ten ticks from 4 m/s: each tick the speed loses 4 % and then moves the cart.
        double expected = 0.0;
        for (int tick = 1; tick <= 10; ++tick) {
            expected += 4.0 * std::pow(QcMinecartMotion::emptyDrag, tick) * QcPhysics::tickDuration;
        }

        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            QList<QcBlockPos> line;
            for (int x = 1; x <= 12; ++x) {
                line.append({x, 0, 8});
            }
            lay(line);
            // A buffer stop at the end of the track.
            put(13, 0, 8, u"stone"_s);
            const quint32 id = cartAt(2, 0, 8, 4.0, 0.0);
            ticks(10);
            const QcVec3 now = local(cart(id).motion.position);
            QVERIFY2(std::abs(now.x - (2.5 + expected)) < 1e-9, qPrintable(u"%1 turns: %2 instead of %3"_s.arg(m_turns).arg(now.x).arg(2.5 + expected)));
            QVERIFY(std::abs(now.z - 8.5) < 1e-9);
            QVERIFY(std::abs(now.y) < 1e-9);

            // It runs into the end of the track before the block and stops just inside the last rail.
            m_server->carts().find(id)->motion.velocity = direction(8.0, 0.0);
            ticks(200);
            const QcVec3 stopped = local(cart(id).motion.position);
            QVERIFY2(stopped.x < 13.0 && stopped.x > 12.9, qPrintable(QString::number(stopped.x)));
            QCOMPARE(cart(id).motion.velocity.length(), 0.0);
            QVERIFY(entityProblems().isEmpty());

            // At rest it is not stepped; taking its rail away wakes it, and it rests again on the
            // ground, off the rails now.
            ticks(2);
            QVERIFY(cart(id).rest.has_value());
            QVERIFY(!cart(id).rest->offRails);
            putAt(at(12, 0, 8), qcAirBlockId);
            ticks(1);
            QVERIFY(cart(id).rest.has_value());
            QVERIFY(cart(id).rest->offRails);
            QVERIFY(!QcMinecartMotion::segmentAt(cart(id).motion.position, m_server->carts().rails(), m_server->world()));
        }
    }

    void cartGoesRoundALoopTheSameInEveryRotation()
    {
        // A 6 x 6 loop of rails; a cart sent round it at full speed stays on the track.
        QList<QcVec3> first;
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            QList<QcBlockPos> loop;
            for (int i = 2; i <= 7; ++i) {
                loop.append({i, 0, 2});
                loop.append({i, 0, 7});
                if (i > 2 && i < 7) {
                    loop.append({2, 0, i});
                    loop.append({7, 0, i});
                }
            }
            lay(loop);
            QCOMPARE(ends(2, 0, 2), u"east,south"_s);
            const quint32 id = cartAt(4, 0, 2, QcMinecartMotion::maxSpeed, 0.0);
            m_server->carts().find(id)->rider = 0;

            QList<QcVec3> path;
            for (int tick = 0; tick < 40; ++tick) {
                ticks(1);
                const QcVec3 at = cart(id).motion.position;
                QVERIFY(QcMinecartMotion::segmentAt(at, m_server->carts().rails(), m_server->world()));
                path.append(local(at));
            }
            // It went round at least one corner.
            QVERIFY(path.constLast().z > 2.6);
            if (m_turns == 0) {
                first = path;
                continue;
            }
            for (qsizetype i = 0; i < path.size(); ++i) {
                QVERIFY2((path[i] - first[i]).length() < 1e-9, qPrintable(u"turns %1, tick %2"_s.arg(m_turns).arg(i)));
            }
        }
    }

    void cartRollsDownASlopeAndBackFromOne()
    {
        QList<QcVec3> first;
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // A rail on a block at the west end, a slope down to the east, then level track.
            put(1, 0, 11, u"stone"_s);
            QList<QcBlockPos> track{{1, 1, 11}};
            for (int x = 2; x <= 12; ++x) {
                track.append({x, 0, 11});
            }
            lay(track);
            QCOMPARE(ends(2, 0, 11), u"east,west+"_s);

            const quint32 id = cartAt(1, 1, 11, 1.0, 0.0);
            double fastest = 0.0;
            for (int tick = 0; tick < 60; ++tick) {
                ticks(1);
                fastest = std::max(fastest, cart(id).motion.velocity.length());
            }
            // Down the slope it got faster than it started, and it is on the level track now.
            QVERIFY2(fastest > 1.5, qPrintable(QString::number(fastest)));
            const QcVec3 below = local(cart(id).motion.position);
            QVERIFY2(below.x > 3.0 && std::abs(below.y) < 1e-9, qPrintable(u"%1 %2"_s.arg(below.x).arg(below.y)));

            // Sent back up slowly it does not get over the top and rolls down again.
            m_server->carts().find(id)->motion.position = point(4.5, 0.0, 11.5);
            m_server->carts().find(id)->motion.velocity = direction(-2.5, 0.0);
            QList<QcVec3> path;
            for (int tick = 0; tick < 60; ++tick) {
                ticks(1);
                path.append(local(cart(id).motion.position));
            }
            const double highest = std::ranges::max(path, {}, &QcVec3::y).y;
            QVERIFY2(highest > 0.1 && highest < 1.0, qPrintable(QString::number(highest)));
            QVERIFY(path.constLast().x > 3.0);
            if (m_turns == 0) {
                first = path;
                continue;
            }
            for (qsizetype i = 0; i < path.size(); ++i) {
                QVERIFY2((path[i] - first[i]).length() < 1e-9, qPrintable(u"turns %1, tick %2"_s.arg(m_turns).arg(i)));
            }
        }
    }

    void cartsTradeTheirSpeed()
    {
        buildArea();
        QList<QcBlockPos> line;
        for (int x = 0; x <= 13; ++x) {
            line.append({x, 0, 6});
        }
        lay(line);
        const quint32 moving = cartAt(2, 0, 6, 4.0, 0.0);
        const quint32 resting = cartAt(6, 0, 6);
        ticks(150);
        // The moving cart passed most of its speed on (restitution 0.5: three quarters) and stayed
        // behind; the cart it hit rolled further than it.
        const double hit = local(cart(resting).motion.position).x - 6.5;
        QVERIFY2(hit > 1.0, qPrintable(QString::number(hit)));
        QCOMPARE(cart(resting).motion.velocity.length(), 0.0);
        QVERIFY(local(cart(moving).motion.position).x < local(cart(resting).motion.position).x - 0.9);
    }

    void cartsOnTheSameSpotMeetAlongTheirTrack()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            lay({{5, 0, 4}, {5, 0, 5}, {5, 0, 6}});
            const quint32 first = cartAt(5, 0, 5);
            const quint32 second = cartAt(5, 0, 5);
            const QList<QcMinecartCollisions::Pair> pairs = QcMinecartCollisions::touching(m_server->carts().all(), m_server->carts().rails(), m_server->world());
            QCOMPARE(pairs.size(), 1);
            // Along the north-south track (whichever way), from the lower id to the higher.
            const QcVec3 along = direction(0.0, 1.0);
            QVERIFY2(std::abs(std::abs(pairs.constFirst().line.dot(along)) - 1.0) < 1e-9, qPrintable(u"turns %1"_s.arg(m_turns)));
            const qsizetype lower = m_server->carts().all()[pairs.constFirst().first].id == std::min(first, second) ? 0 : 1;
            const QList<QcVec3> flipped = {pairs.constFirst().line, pairs.constFirst().line * -1.0};
            QVERIFY((flipped[lower] - pairs.constFirst().line).length() < 1e-9);
        }
    }

    void cartsRollingIntoADeadEndDownASlopeStayApart()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // A slope down to the east from y 4 to a dead end at its foot (x 5), on stone steps; a
            // cart on each of the three lowest slope rails.
            for (int x = 1; x <= 5; ++x) {
                for (int y = 0; y < 5 - x; ++y) {
                    put(x, y, 6, u"stone"_s);
                }
            }
            lay({{1, 4, 6}, {2, 3, 6}, {3, 2, 6}, {4, 1, 6}, {5, 0, 6}});
            QCOMPARE(ends(5, 0, 6), u"east,west+"_s);
            const QList<quint32> ids{cartAt(3, 2, 6), cartAt(4, 1, 6), cartAt(5, 0, 6)};
            double closest = 2.0;
            for (int tick = 0; tick < 400; ++tick) {
                ticks(1);
                for (qsizetype i = 0; i + 1 < ids.size(); ++i) {
                    const QcVec3 between = cart(ids[i + 1]).motion.position - cart(ids[i]).motion.position;
                    closest = std::min(closest, std::hypot(between.x, between.z));
                }
            }
            QVERIFY2(closest > 0.6, qPrintable(u"turns %1: %2"_s.arg(m_turns).arg(closest)));
            QVERIFY(entityProblems().isEmpty());
        }
    }

    void aCartHeldAtTheFootOfADeadEndSlopeRests()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            put(1, 0, 9, u"stone"_s);
            // A buffer stop at the foot of the slope.
            put(3, 0, 9, u"stone"_s);
            lay({{1, 1, 9}, {2, 0, 9}});
            QCOMPARE(ends(2, 0, 9), u"east,west+"_s);
            const quint32 id = cartAt(1, 1, 9, 1.0, 0.0);
            ticks(100);
            // It stands at the lower end of the slope and is no longer stepped.
            const QcVec3 held = cart(id).motion.position;
            QVERIFY2(local(held).x > 2.9 && local(held).x < 3.0, qPrintable(QString::number(local(held).x)));
            QVERIFY(cart(id).rest.has_value());
            ticks(20);
            QCOMPARE(cart(id).motion.position, held);
            QVERIFY(cart(id).rest.has_value());

            // A rail laid below its end lets it roll on.
            put(3, 0, 9, u"air"_s);
            put(3, -1, 9, u"air"_s);
            put(3, -2, 9, u"stone"_s);
            lay({{3, -1, 9}});
            ticks(20);
            QVERIFY2(local(cart(id).motion.position).x > 3.0, qPrintable(QString::number(local(cart(id).motion.position).x)));
        }
    }

    void pistonPushesACartAlongItsRail()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            put(0, 0, 3, u"lever[attach=floor]"_s);
            put(1, 0, 3, u"signal_piston[facing=east]"_s);
            QList<QcBlockPos> line;
            for (int x = 2; x <= 6; ++x) {
                line.append({x, 0, 3});
            }
            lay(line);
            const quint32 id = cartAt(2, 0, 3);
            ticks(2);
            QCOMPARE(local(cart(id).motion.position).x, 2.5);

            flip(0, 0, 3);
            ticks(3);
            // The head takes the cart's cell and pushes the rails on by one: the cart is pushed out of
            // the cell along with them and stands on the track again.
            QVERIFY(isBlock(2, 0, 3, "signal_piston_head"));
            QVERIFY(isBlock(7, 0, 3, "rail"));
            const QcVec3 pushed = local(cart(id).motion.position);
            QVERIFY2(pushed.x - QcMinecartBody::width / 2.0 >= 3.0 - 0.05, qPrintable(QString::number(pushed.x)));
            QVERIFY(std::abs(pushed.z - 3.5) < 1e-9);
            QVERIFY(QcMinecartMotion::segmentAt(cart(id).motion.position, m_server->carts().rails(), m_server->world()));
        }
    }

    void playerPlacesRidesSteersLeavesAndBreaksACart()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/give rail 8"_s);
        chat(u"/give minecart"_s);
        ticks(2);

        // Rails east of the player, placed as a player does.
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:rail"))});
        for (int x = 1; x <= 3; ++x) {
            send(QcMsg::UseItem{m_feet.offset(x, -1, 0), QcBlockFace::Up});
        }
        ticks(2);
        const QcBlockId middle = *m_server->world().blockAt(m_feet.offset(2, 0, 0));
        const std::optional<QcRails::Track> track = QcRails::trackOf(*m_content, middle);
        QVERIFY(track);
        QVERIFY(track->ends[0].side == QcBlockFace::East || track->ends[0].side == QcBlockFace::West);

        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:minecart"))});
        send(QcMsg::UseItem{m_feet.offset(2, 0, 0), QcBlockFace::Up});
        QCOMPARE(m_server->carts().all().size(), 1);
        QCOMPARE(countOf(item("qtcraft:minecart")), 0);
        const quint32 id = m_server->carts().all().constFirst().id;

        send(QcMsg::UseEntity{id});
        idle(2);
        QCOMPARE(m_client->vehicle, id);
        QCOMPARE(cart(id).rider, m_client->entityId);
        const QcVec3 seat = cart(id).motion.position + QcVec3{0.0, QcMinecartBody::seatOffset, 0.0};
        QVERIFY((m_client->motion.position - seat).length() < 1e-9);
        QVERIFY(entityProblems().isEmpty());

        // Forward while looking east pushes the cart east; the rider goes with it.
        for (int i = 0; i < 5; ++i) {
            QcPlayerInput input;
            input.sequence = ++m_sequence;
            input.forward = 1.0f;
            input.yaw = 90.0f;
            m_client->connection.send(QcMsg::PlayerInput{input});
            pump();
            ticks(1);
        }
        QVERIFY(cart(id).motion.position.x > m_feet.x + 2.5);
        QCOMPARE(m_server->entities().minecarts().ridden(m_client->entityId), m_server->carts().find(id));

        // Sneak gets the player out.
        QcPlayerInput sneak;
        sneak.sequence = ++m_sequence;
        sneak.sneak = true;
        m_client->connection.send(QcMsg::PlayerInput{sneak});
        pump();
        ticks(1);
        idle(1);
        QCOMPARE(m_client->vehicle, 0u);
        QCOMPARE(cart(id).rider, 0u);

        // A hit breaks it; it drops its item.
        idle(10);
        send(QcMsg::AttackEntity{id});
        QVERIFY(m_server->carts().all().isEmpty());
        QCOMPARE(droppedCount(item("qtcraft:minecart")), 1);
        QVERIFY(entityProblems().isEmpty());
    }

    void aCartLeavesWithItsChunkAndComesBack()
    {
        m_unloadChunks = true;
        buildArea();
        // Far away, in chunks nobody needs: a cart on a short track.
        const QcBlockPos far = m_feet.offset(6 * QcWorld::chunkSize, 0, 0);
        clearArea(far);
        lay({{3, 0, 3}, {4, 0, 3}, {5, 0, 3}});
        const quint32 id = cartAt(4, 0, 3);
        const QcVec3 where = cart(id).motion.position;
        const QcChunkPos chunk = QcPhysics::blockContaining(where).chunk();
        for (int i = 0; i < 200 && m_server->world().chunk(chunk); ++i) {
            ticks(1);
        }
        QVERIFY(!m_server->world().chunk(chunk));
        QVERIFY(m_server->carts().all().isEmpty());

        // Nobody needs the chunk, so it is looked at before the next tick would let it go again.
        m_server->world().loadChunk(chunk);
        QCOMPARE(m_server->carts().all().size(), 1);
        QCOMPARE(m_server->carts().all().constFirst().motion.position, where);
        QCOMPARE(m_server->carts().all().constFirst().item, u"qtcraft:minecart"_s);
        QVERIFY(entityProblems().isEmpty());
        m_unloadChunks = false;
    }
};

QTEST_GUILESS_MAIN(TestMinecarts)
#include "test_minecarts.moc"
