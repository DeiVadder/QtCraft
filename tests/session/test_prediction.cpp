// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcPrediction.h"
#include "clientcore/session/QcClientEntities.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcTestWorld.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

// QcPrediction on its own: the reconciliation results, the cap of pending inputs, riding a cart and
// the wrapped yaw the client sends with UseItem.
class TestPrediction : public QObject
{
    Q_OBJECT

private:
    // A floor of stone at y = 0, the player standing on it at (0.5, 1, 0.5).
    static QcMsg::PlayerState standingState(quint32 acknowledged)
    {
        QcMsg::PlayerState state;
        state.lastInputSequence = acknowledged;
        state.motion.position = {0.5, 1.0, 0.5};
        state.motion.onGround = true;
        return state;
    }

    void layFloor(QcTestWorld &world) const
    {
        world.fillBox({-8, 0, -8}, {8, 0, 8}, *m_content.blockId(QStringLiteral("test:stone")));
    }

    // The server's side of one player: it applies the inputs two ticks late with its own speed
    // factor from the status effects (as QcServer does) and answers each tick with its state.
    struct LaggingServer
    {
        QcEntityMotion motion;
        float speedFactor = 1.0f;
        QList<QcPlayerInput> queue;
        quint32 acknowledged = 0;

        QcMsg::PlayerState receive(const QcPlayerInput &input, const QcBlockAccess &world)
        {
            queue.append(input);
            if (queue.size() > 2) {
                QcPlayerInput applied = queue.takeFirst();
                applied.effects.speed = speedFactor;
                motion.stepPlayer(applied, world, QcPhysics::tickDuration, QcMovement::Walk);
                acknowledged = applied.sequence;
            }
            QcMsg::PlayerState state;
            state.lastInputSequence = acknowledged;
            state.motion = motion;
            return state;
        }
    };

    // Walks `ticks` ticks forward against the server; returns the largest correction.
    static double walk(QcPrediction &prediction, LaggingServer &server, const QcBlockAccess &world, int ticks)
    {
        double largest = 0.0;
        for (int tick = 0; tick < ticks; ++tick) {
            const QcMsg::PlayerState state = server.receive(prediction.predict(world, false, QcMovement::Walk), world);
            prediction.reconcile(state, &world, QcMovement::Walk);
            largest = std::max(largest, prediction.lastCorrection());
        }
        return largest;
    }

    QcContentRegistry m_content = qcMakeTestContent();

private slots:
    // A tiny negative yaw rounds to exactly 360 when wrapped naively; the intent stays below 360 and a
    // UseItem carrying it decodes (decision E-853).
    void aTinyNegativeYawWrapsBelow360AndDecodes()
    {
        QcPrediction prediction;
        QcPlayerInput intent;
        intent.yaw = -1e-6f;
        prediction.setIntent(intent);
        QVERIFY(prediction.intent().yaw >= 0.0f);
        QVERIFY(prediction.intent().yaw < 360.0f);
        QCOMPARE(qcWrappedYaw(360.0f), 0.0f);
        QCOMPARE(qcWrappedYaw(-90.0f), 270.0f);
        QCOMPARE(qcWrappedYaw(std::numeric_limits<float>::quiet_NaN()), 0.0f);

        const QcMsg::UseItem sent{{1, 2, 3}, QcBlockFace::Up, {0.5, 1.0, 0.5}, 7, QcMsg::UseItem::View{prediction.intent().yaw, 0.0f}};
        const QcResult<QcMessage> result = QcProtocol::decode(QcProtocol::encode(QcMessage(sent)), QcProtocol::Peer::Client);
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        const auto &decoded = std::get<QcMsg::UseItem>(*result);
        QVERIFY(decoded.view.has_value());
        QCOMPARE(decoded.view->yaw, prediction.intent().yaw);
    }

    void theFirstStatePlacesAndLaterOnesApply()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        QVERIFY(!prediction.hasServerState());

        QCOMPARE(prediction.reconcile(standingState(0), &world, QcMovement::Walk), QcPrediction::Reconciled::Placed);
        QVERIFY(prediction.hasServerState());
        // A placement is no correction, wherever the prediction stood before.
        QCOMPARE(prediction.lastCorrection(), 0.0);
        QCOMPARE(prediction.motion().position, (QcVec3{0.5, 1.0, 0.5}));

        QCOMPARE(prediction.reconcile(standingState(0), &world, QcMovement::Walk), QcPrediction::Reconciled::Applied);
    }

    void anOlderStateIsIgnored()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        QCOMPARE(prediction.reconcile(standingState(5), &world, QcMovement::Walk), QcPrediction::Reconciled::Placed);

        QcMsg::PlayerState older = standingState(4);
        older.motion.position = {3.5, 1.0, 3.5};
        QCOMPARE(prediction.reconcile(older, &world, QcMovement::Walk), QcPrediction::Reconciled::Ignored);
        QCOMPARE(prediction.motion().position, (QcVec3{0.5, 1.0, 0.5}));
    }

    // A tick without a step (server paused, decision E-960) while the walk key is held: the state
    // before becomes the current one, so the frames between ticks draw the player at one place
    // instead of swinging back over the last step with every tick; a pending input stays pending.
    void restingTicksStandStill()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);
        QcPlayerInput intent;
        intent.forward = 1.0f;
        prediction.setIntent(intent);
        for (int tick = 0; tick < 3; ++tick) {
            std::ignore = prediction.predict(world, false, QcMovement::Walk);
        }
        const QcVec3 walked = prediction.motion().position;
        QVERIFY(prediction.previousMotion().position != walked);

        for (int tick = 0; tick < 20; ++tick) {
            prediction.rest();
            QCOMPARE(prediction.previousMotion().position, walked);
            QCOMPARE(prediction.motion().position, walked);
        }
        QCOMPARE(prediction.intent().forward, 1.0f);
        QCOMPARE(prediction.pendingInputCount(), 3);
    }

    void acknowledgedInputsLeaveAndTheRestIsReplayed()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);
        QcPlayerInput intent;
        intent.forward = 1.0f;
        prediction.setIntent(intent);

        for (int tick = 0; tick < 4; ++tick) {
            QCOMPARE(prediction.predict(world, false, QcMovement::Walk).sequence, quint32(tick + 1));
        }
        QCOMPARE(prediction.pendingInputCount(), 4);
        const QcVec3 predicted = prediction.motion().position;

        // The server acknowledged two inputs at the predicted start: replaying the other two moves
        // the player less far than the four did, which shows as a correction.
        QCOMPARE(prediction.reconcile(standingState(2), &world, QcMovement::Walk), QcPrediction::Reconciled::Applied);
        QCOMPARE(prediction.pendingInputCount(), 2);
        QVERIFY(prediction.lastCorrection() > 0.0);
        QCOMPARE(prediction.largestCorrection(), prediction.lastCorrection());
        QVERIFY(prediction.motion().position != predicted);
    }

    void pendingInputsStopAtTheCap()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);

        for (qsizetype tick = 0; tick < QcPrediction::maxPendingInputs + 20; ++tick) {
            prediction.predict(world, false, QcMovement::Walk);
        }
        QCOMPARE(prediction.pendingInputCount(), QcPrediction::maxPendingInputs);

        // The oldest were dropped: acknowledging the first 20 leaves all that are kept.
        prediction.reconcile(standingState(20), &world, QcMovement::Walk);
        QCOMPARE(prediction.pendingInputCount(), QcPrediction::maxPendingInputs);
        prediction.reconcile(standingState(21), &world, QcMovement::Walk);
        QCOMPARE(prediction.pendingInputCount(), QcPrediction::maxPendingInputs - 1);
    }

    void aRiderIsNeitherPredictedNorCorrected()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcClientEntities entities;
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);

        // Getting in: the server names the cart; the state is no correction and is not replayed.
        QcMsg::PlayerState riding = standingState(0);
        riding.vehicle = 7;
        riding.motion.position = {4.5, 1.0, 4.5};
        QCOMPARE(prediction.reconcile(riding, &world, QcMovement::Walk), QcPrediction::Reconciled::Applied);
        QVERIFY(prediction.riding().isRiding());
        QCOMPARE(prediction.riding().vehicle(), quint32(7));
        QCOMPARE(prediction.lastCorrection(), 0.0);
        QCOMPARE(prediction.motion().position, (QcVec3{0.5, 1.0, 0.5}));

        // While the cart has not arrived in the mirror the player stays; the input is still sent.
        QcPlayerInput intent;
        intent.forward = 1.0f;
        prediction.setIntent(intent);
        prediction.predict(world, false, QcMovement::Walk);
        prediction.followCart(entities, m_content);
        QCOMPARE(prediction.motion().position, (QcVec3{0.5, 1.0, 0.5}));
        QCOMPARE(prediction.pendingInputCount(), 1);

        // Getting out places the player where the server says, again without a correction.
        QcMsg::PlayerState out = standingState(1);
        out.motion.position = {6.5, 1.0, 6.5};
        QCOMPARE(prediction.reconcile(out, &world, QcMovement::Walk), QcPrediction::Reconciled::Applied);
        QVERIFY(!prediction.riding().isRiding());
        QCOMPARE(prediction.lastCorrection(), 0.0);
        QCOMPARE(prediction.motion().position, (QcVec3{6.5, 1.0, 6.5}));
    }

    // A cart rider while the server pauses (decision E-960): the session keeps its own motion on
    // the seat when it takes over the core's prediction (mirror), so the core's rest does not reach
    // it; resting the session's prediction as well makes the drawn rider stand still.
    void aPausedRiderRestsOnTheSeat()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcClientEntities entities;
        entities.setBuffered(false);
        QcMsg::EntitySpawn cart;
        cart.id = 7;
        cart.name = QStringLiteral("Cart");
        cart.position = {4.5, 1.0, 4.5};
        QVERIFY(entities.spawn(cart, &m_content));
        QcPrediction session;
        session.reconcile(standingState(0), &world, QcMovement::Walk);
        QcMsg::PlayerState riding = standingState(0);
        riding.vehicle = cart.id;
        session.reconcile(riding, &world, QcMovement::Walk);
        QVERIFY(session.riding().ridesCart());

        // The cart rolls two ticks; the session follows it onto the seat.
        for (qint64 tick = 1; tick <= 2; ++tick) {
            QcMsg::EntityMoves moves;
            moves.timeMs = tick * 50;
            moves.moves = {{cart.id, QcVec3{4.5 + static_cast<double>(tick), 1.0, 4.5}, 0.0f, 20.0, false, false, 0.0f, QcMsg::OnGround, qcNoItem}};
            entities.moves(moves, tick * 50);
            entities.tick(tick * 50);
            session.followCart(entities, m_content);
        }
        const QcVec3 seat = session.motion().position;
        QVERIFY(session.previousMotion().position != seat);

        // Paused: the core rests, the session takes its prediction but keeps its own motion.
        QcPrediction core = session;
        core.rest();
        session.mirror(core, true);
        QVERIFY(session.previousMotion().position != seat);
        session.rest();
        QCOMPARE(session.previousMotion().position, seat);
        QCOMPARE(session.motion().position, seat);
    }

    // Slowness II (speed factor 0.7, decision E-838): the server moves each input with its own
    // factor. While the client knows the same factor its inputs carry it, so replaying them on the
    // server's states corrects nothing; when the slowness ends the client learns it a few ticks
    // late, which may correct once, and then the corrections are zero again.
    void slownessKeepsTheCorrectionAtZero()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);
        QcPlayerInput intent;
        intent.forward = 1.0f;
        prediction.setIntent(intent);
        LaggingServer server;
        server.motion = standingState(0).motion;

        server.speedFactor = 0.7f;
        prediction.setMovementEffects({.speed = 0.7f}, 1);
        QCOMPARE(walk(prediction, server, world, 12), 0.0);
        const double slowStretch = (server.motion.position - QcVec3{0.5, 1.0, 0.5}).length();
        QVERIFY(slowStretch > 0.5);

        // The slowness ends on the server; the client hears of it two ticks later.
        server.speedFactor = 1.0f;
        walk(prediction, server, world, 2);
        prediction.setMovementEffects({}, 2);
        walk(prediction, server, world, 3);
        const QcVec3 settled = server.motion.position;
        QCOMPARE(walk(prediction, server, world, 8), 0.0);

        // Without slowness the same walk covers more ground: the factor was applied at all.
        QVERIFY((server.motion.position - settled).length() > slowStretch * 8.0 / 12.0 * 1.2);
    }

    // The mark of a draw (decision E-986) rides with each input: the replay moves as predicted, and
    // both sides slow on the same input whatever the delay.
    void aDrawKeepsTheCorrectionAtZero()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);
        prediction.setIntent(QcPlayerInput{.forward = 1.0f, .sprint = true});
        LaggingServer server;
        server.motion = standingState(0).motion;

        QCOMPARE(walk(prediction, server, world, 6), 0.0);
        prediction.setUsingItem(true);
        // The session sets its intent every frame; the draw stays.
        prediction.setIntent(QcPlayerInput{.forward = 1.0f, .sprint = true});
        QVERIFY(prediction.intent().usingItem);
        const QcVec3 before = server.motion.position;
        QCOMPARE(walk(prediction, server, world, 10), 0.0);
        prediction.setUsingItem(false);
        QCOMPARE(walk(prediction, server, world, 5), 0.0);
        QVERIFY(server.motion.position.length() > before.length());
    }

    void theDrawSlowsEveryKindOfMove()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        const auto speedOf = [&world](QcPlayerInput input, QcMovement movement) {
            QcEntityMotion motion = standingState(0).motion;
            motion.position.y = input.flying ? 3.0 : 1.0;
            input.forward = 1.0f;
            for (int tick = 0; tick < 16; ++tick) {
                motion.position.x = 0.5;
                motion.position.z = 0.5;
                motion.stepPlayer(input, world, QcPhysics::tickDuration, movement);
            }
            return std::hypot(motion.velocity.x, motion.velocity.z);
        };
        const double factor = QcPhysics::usingItemFactor;
        QCOMPARE(factor, 0.2);
        for (const QcPlayerInput &input : {QcPlayerInput{}, QcPlayerInput{.sneak = true}}) {
            QcPlayerInput drawn = input;
            drawn.usingItem = true;
            QVERIFY(std::abs(speedOf(drawn, QcMovement::Walk) - speedOf(input, QcMovement::Walk) * factor) < 0.01);
        }
        // Drawn, the wish to sprint is plain walking; flying likewise.
        QVERIFY(std::abs(speedOf({.sprint = true, .usingItem = true}, QcMovement::Walk) - QcPhysics::walkSpeed * factor) < 0.05);
        const QcPlayerInput flight{.flying = true};
        QVERIFY(std::abs(speedOf({.sprint = true, .flying = true, .usingItem = true}, QcMovement::MayFly) - speedOf(flight, QcMovement::MayFly) * factor) < 0.05);

        QVERIFY(QcPlayerInput({.forward = 1.0f, .sprint = true}).sprints());
        QVERIFY(!QcPlayerInput({.forward = 1.0f, .sprint = true, .usingItem = true}).sprints());
    }

    // A state counts against a draw only when it answers an input of that draw: the answers to the
    // inputs before it, or to the end of an earlier draw, say nothing about this one.
    void onlyAnAnswerToTheDrawRefusesIt()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);
        const auto refused = [&prediction](quint32 acknowledged, bool serverDraws) {
            QcMsg::PlayerState state = standingState(acknowledged);
            state.usingItem = serverDraws;
            return prediction.usingItemRefused(state);
        };

        const QcPlayerInput first = prediction.predict(world, false, QcMovement::Walk);
        QVERIFY(!refused(first.sequence, false));
        prediction.setUsingItem(true);
        QVERIFY(!refused(first.sequence, false));
        const QcPlayerInput drawn = prediction.predict(world, false, QcMovement::Walk);
        QVERIFY(drawn.usingItem);
        QVERIFY(refused(drawn.sequence, false));
        QVERIFY(!refused(drawn.sequence, true));

        // A new draw after a release: the old answer does not reach it.
        prediction.setUsingItem(false);
        std::ignore = prediction.predict(world, false, QcMovement::Walk);
        prediction.setUsingItem(true);
        QVERIFY(!refused(drawn.sequence, false));
        QVERIFY(refused(prediction.predict(world, false, QcMovement::Walk).sequence, false));
    }

    // A knockback (decision E-971) is a velocity the server gives the player between two inputs: the
    // state that brings it corrects the prediction once by what the push did over the inputs still
    // pending, then the replayed inputs carry the push on, so nothing pulls the player back.
    void aKnockbackCorrectsOnceAndCarriesOn()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);
        LaggingServer server;
        server.motion = standingState(0).motion;
        QCOMPARE(walk(prediction, server, world, 6), 0.0);

        // A blow from the west: 6 m/s east and 5 m/s up, as QcMobManager::knockbackVelocity gives.
        server.motion.velocity = {6.0, 5.0, 0.0};
        server.motion.onGround = false;
        const QcMsg::PlayerState knocked = server.receive(prediction.predict(world, false, QcMovement::Walk), world);
        prediction.reconcile(knocked, &world, QcMovement::Walk);
        QVERIFY(prediction.lastCorrection() > 0.0);
        QVERIFY(prediction.motion().velocity.x > 0.0);

        // From then on the prediction is where the server is and flies on east, without a correction.
        double x = prediction.motion().position.x;
        double largest = 0.0;
        for (int tick = 0; tick < 12; ++tick) {
            largest = std::max(largest, walk(prediction, server, world, 1));
            QVERIFY2(prediction.motion().position.x >= x, qPrintable(QString::number(prediction.motion().position.x)));
            x = prediction.motion().position.x;
        }
        QVERIFY2(largest < 1e-9, qPrintable(QString::number(largest)));
        QVERIFY2(x > 1.0, qPrintable(QString::number(x)));
        QVERIFY(prediction.motion().onGround);
    }

    // The edge guard of sneaking (decision E-961) runs in the shared physics: the server stops the
    // player at the floor's edge on the same tick as the prediction, so nothing is corrected, and
    // the player stays on the floor.
    void sneakingStopsAtTheEdgeWithoutCorrection()
    {
        QcTestWorld world(m_content);
        layFloor(world);
        QcPrediction prediction;
        prediction.reconcile(standingState(0), &world, QcMovement::Walk);
        QcPlayerInput intent;
        intent.forward = 1.0f;
        intent.sneak = true;
        prediction.setIntent(intent);
        LaggingServer server;
        server.motion = standingState(0).motion;

        // North (-z) towards the edge at z = -8, 8.5 m away: 1.3 m/s reach it in 7 s.
        QCOMPARE(walk(prediction, server, world, 180), 0.0);
        QVERIFY2(server.motion.position.z < -8.0 && server.motion.position.z >= -8.0 - QcPhysics::playerWidth * 0.5,
                 qPrintable(QString::number(server.motion.position.z)));
        QCOMPARE(server.motion.position.y, 1.0);
        QCOMPARE(prediction.motion().position, server.motion.position);
    }

    // The eyes drop while the player crouches (decision E-961): sneaking on foot, not in flight
    // (where sneak descends).
    void theEyesDropWhileCrouching()
    {
        QcPrediction prediction;
        QCOMPARE(prediction.eyeHeight(), QcPhysics::playerEyeHeight);
        QcPlayerInput intent;
        intent.sneak = true;
        prediction.setIntent(intent);
        QVERIFY(prediction.crouches());
        QCOMPARE(prediction.eyeHeight(), QcPhysics::sneakEyeHeight);

        QcTestWorld world(m_content);
        QcMsg::PlayerState flying = standingState(0);
        flying.motion.flying = true;
        flying.motion.onGround = false;
        flying.motion.position.y = 5.0;
        prediction.reconcile(flying, &world, QcMovement::MayFly);
        QVERIFY(!prediction.crouches());
        QCOMPARE(prediction.eyeHeight(), QcPhysics::playerEyeHeight);
    }
};

QTEST_GUILESS_MAIN(TestPrediction)
#include "test_prediction.moc"
