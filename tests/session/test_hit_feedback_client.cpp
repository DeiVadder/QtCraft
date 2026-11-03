// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/camera/QcCamera.h"
#include "clientcore/camera/QcHurtTilt.h"
#include "clientcore/player/QcLocalAnimation.h"
#include "clientcore/player/QcPrediction.h"
#include "clientcore/session/QcClientEntities.h"
#include "common/protocol/QcProtocol.h"

#include <QTest>

#include <cmath>
#include <numbers>

// The client's side of hit feedback (decision E-971): the red flash of others starts with the
// knockback the buffered motion shows and lasts the hurt time, the own hurt time with the tilt of
// the view (its curve and directions), the roll of the camera, and the hurt in PlayerHealth on the
// wire.
class TestHitFeedbackClient : public QObject
{
    Q_OBJECT

private:
    static constexpr quint32 bob = 7;
    static constexpr qint64 tickMs = 50;

    static QcMsg::EntityMove moveAt(double x, bool hurt) { return QcMsg::EntityMove{bob, {x, 64.0, 0.5}, 0.0f, 20.0, hurt, 0.0f, 0, qcNoItem}; }

    // A player standing at x = 0 in `entities`, its first snapshot at server time 0.
    static void spawnBob(QcClientEntities &entities)
    {
        QcMsg::EntitySpawn spawn;
        spawn.id = bob;
        spawn.kind = QcEntityKind::Player;
        spawn.position = {0.0, 64.0, 0.5};
        spawn.health = 20.0;
        QVERIFY(entities.spawn(spawn, nullptr));
    }

private slots:
    void theFlashStartsWithTheShownKnockback()
    {
        QcClientEntities entities;
        spawnBob(entities);

        // A snapshot per tick arrives at once (no lag); the hit at server time 1000 throws Bob a metre.
        constexpr qint64 hitMs = 1000;
        int flashStart = -1;
        int flashTicks = 0;
        for (qint64 ms = 0; ms <= 2000; ms += tickMs) {
            entities.moves(QcMsg::EntityMoves{{moveAt(ms >= hitMs ? 1.0 : 0.0, ms == hitMs)}, ms}, ms);
            entities.tick(ms);
            const QcClientEntity &shown = entities.all().value(bob);
            if (shown.hurtTicks > 0) {
                ++flashTicks;
                if (flashStart < 0) {
                    flashStart = static_cast<int>(ms);
                    // The pose at the end of the starting tick is already the thrown one, the one
                    // before it was not.
                    QCOMPARE(shown.position.x, 1.0);
                    QVERIFY(shown.previousPosition.x < 1.0);
                }
            }
        }

        // Shown 100 ms in the past (QcSnapshotClock::baseDelayMs): the flash waits for the motion
        // of the hit instead of starting before the push is seen, and lasts the hurt time.
        QVERIFY2(flashStart >= hitMs + 50 && flashStart <= hitMs + 100, qPrintable(QString::number(flashStart)));
        QCOMPARE(flashTicks, QcClientEntities::hurtFlashTicks);
        QCOMPARE(QcClientEntities::hurtFlashTicks, 10);
    }

    void unbufferedTheFlashStartsAtOnce()
    {
        QcClientEntities entities;
        entities.setBuffered(false);
        spawnBob(entities);
        entities.moves(QcMsg::EntityMoves{{moveAt(0.0, false)}, 0}, 0);
        entities.tick(0);
        QCOMPARE(entities.all().value(bob).hurtTicks, 0);

        entities.moves(QcMsg::EntityMoves{{moveAt(1.0, true)}, tickMs}, tickMs);
        QCOMPARE(entities.all().value(bob).hurtTicks, QcClientEntities::hurtFlashTicks);
        int shown = 0;
        for (int tick = 1; tick <= 20; ++tick) {
            entities.tick(tickMs * (tick + 1));
            shown += entities.all().value(bob).hurtTicks > 0 ? 1 : 0;
        }
        QCOMPARE(shown, QcClientEntities::hurtFlashTicks - 1);
    }

    void theTiltRisesFastAndEasesBack()
    {
        // The reference's curve: nothing at the hit and at the end, the top a sixth of the way in.
        QCOMPARE(QcHurtTilt::strength(0.0), std::sin(std::numbers::pi));
        QVERIFY(QcHurtTilt::strength(0.0) < 1e-9);
        QVERIFY(QcHurtTilt::strength(1.0) < 1e-9);
        QCOMPARE(QcHurtTilt::strength(-0.1), 0.0);
        QCOMPARE(QcHurtTilt::strength(1.1), 0.0);
        QCOMPARE(QcHurtTilt::strength(std::nan("")), 0.0);
        const double top = 1.0 - std::pow(0.5, 0.25);
        QVERIFY(std::abs(QcHurtTilt::strength(top) - 1.0) < 1e-9);
        QVERIFY(QcHurtTilt::strength(0.1) > 0.8);
        QVERIFY(QcHurtTilt::strength(0.5) < 0.25);
        QVERIFY(QcHurtTilt::strength(0.5) > QcHurtTilt::strength(0.7));

        // From the front the view tips up, from the right it leans left, from behind down, without
        // a direction it rolls.
        const QcHurtTilt::Tilt front = QcHurtTilt::at(top, 0.0);
        QCOMPARE(front.pitch, QcHurtTilt::maxDegrees);
        QVERIFY(std::abs(front.roll) < 1e-9);
        const QcHurtTilt::Tilt right = QcHurtTilt::at(top, 90.0);
        QVERIFY(std::abs(right.pitch) < 1e-9);
        QCOMPARE(right.roll, -QcHurtTilt::maxDegrees);
        QVERIFY(QcHurtTilt::at(top, 180.0).pitch < -QcHurtTilt::maxDegrees + 1e-9);
        QCOMPARE(QcHurtTilt::at(top, std::nullopt), (QcHurtTilt::Tilt{0.0, QcHurtTilt::maxDegrees}));
        QCOMPARE(QcHurtTilt::at(-1.0, 90.0), QcHurtTilt::Tilt{});
    }

    void theOwnHurtTiltsAwayFromTheAttacker()
    {
        QcLocalAnimation animation;
        QcPrediction prediction;
        QCOMPARE(animation.hurtTilt(0.5), QcHurtTilt::Tilt{});
        QCOMPARE(animation.localPose(0.5, prediction, -1.0).hurtProgress, -1.0);

        // Looking east (yaw 90), hit from the south (yaw 180): the hit came from the right.
        animation.hurt(180.0f, 90.0f);
        QCOMPARE(animation.localPose(0.0, prediction, -1.0).hurtProgress, 0.0);
        animation.tick(qcNoItem, false, {});
        const QcHurtTilt::Tilt tilt = animation.hurtTilt(0.6);
        QVERIFY2(tilt.roll < -0.8 * QcHurtTilt::maxDegrees, qPrintable(QString::number(tilt.roll)));
        QVERIFY(std::abs(tilt.pitch) < 1e-3);

        // The hurt time is the flash of others; afterwards the view stands straight again.
        for (int tick = 1; tick < QcLocalAnimation::hurtTicks; ++tick) {
            QVERIFY(animation.localPose(0.0, prediction, -1.0).hurtProgress >= 0.0);
            animation.tick(qcNoItem, false, {});
        }
        QCOMPARE(animation.localPose(0.0, prediction, -1.0).hurtProgress, -1.0);
        QCOMPARE(animation.hurtTilt(0.5), QcHurtTilt::Tilt{});

        // A fall has no direction: the view rolls.
        animation.hurt(std::nullopt, 0.0f);
        animation.tick(qcNoItem, false, {});
        QVERIFY(animation.hurtTilt(0.6).roll > 0.8 * QcHurtTilt::maxDegrees);
    }

    void theCameraRollsAboutItsViewDirection()
    {
        QcCamera camera;
        camera.yaw = 30.0f;
        camera.pitch = -10.0f;
        const QVector3D ahead = camera.forward() * 5.0f;
        const QVector3D straight = camera.viewRotation().map(QVector3D(0.0f, 1.0f, 0.0f));

        camera.roll = 20.0f;
        // What lies ahead stays ahead; the world's up turns anticlockwise in view (the head leans right).
        QVERIFY((camera.viewRotation().map(ahead) - QVector3D(0.0f, 0.0f, -5.0f)).length() < 1e-4f);
        const QVector3D rolled = camera.viewRotation().map(QVector3D(0.0f, 1.0f, 0.0f));
        QVERIFY(std::abs(rolled.length() - straight.length()) < 1e-4f);
        QVERIFY(rolled.x() < straight.x() - 0.1f);
    }

    void healthCarriesTheHurtOnTheWire()
    {
        const auto roundTrip = [](const QcMsg::PlayerHealth &health) {
            const QcResult<QcMessage> decoded = QcProtocol::decode(QcProtocol::encode(health), QcProtocol::Peer::Server);
            return decoded ? std::get<QcMsg::PlayerHealth>(*decoded) : QcMsg::PlayerHealth{};
        };
        const QcMsg::PlayerHealth hit = roundTrip(QcMsg::PlayerHealth{17.0, 20.0, false, false, {}, true, -90.0f});
        QCOMPARE(hit.health, 17.0);
        QVERIFY(hit.hurt);
        QCOMPARE(hit.hurtFrom, std::optional<float>(270.0f));
        const QcMsg::PlayerHealth fall = roundTrip(QcMsg::PlayerHealth{17.0, 20.0, false, false, {}, true});
        QVERIFY(fall.hurt);
        QVERIFY(!fall.hurtFrom);
        QVERIFY(!roundTrip(QcMsg::PlayerHealth{20.0, 20.0, false}).hurt);
    }
};

QTEST_GUILESS_MAIN(TestHitFeedbackClient)
#include "test_hit_feedback_client.moc"
