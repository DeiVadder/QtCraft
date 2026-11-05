// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "fixtures/QcLatencyTransport.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <QTest>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;
constexpr int delayMs = 100;
constexpr int jitterMs = 50;
constexpr int walkTicks = 30;
// The speed a walk reaches is this close to the wanted one.
constexpr double speedTolerance = 0.05;
// Client ticks clearly below the shortest round trip of the link (2 x delayMs): what ended by then
// did not wait for an answer of the server.
constexpr int ticksBelowRoundTrip = 3;
// The ticks the session's mirror of the prediction may lag behind the core.
constexpr quint32 mirrorLag = 2;

} // namespace

// The prediction of a player who draws a bow across a link with a round trip of 200 to 300 ms
// (decision E-986): client and server slow him on the same input, so walking while drawing,
// releasing, cancelling and switching the slot costs no correction at all, also when the server
// refuses a draw the client began; every way out of a draw leaves the normal speed.
class TestDrawClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    int m_ticks = 0;
    // Whether a draw ran when the session heard of the death (aDeathLeavesNoSlowdown).
    std::optional<bool> m_drawingAtDeath;

    void start()
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.seed = 1;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        config.defaultGameMode = QcGameMode::Survival;
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, QcWorldGenerators::create(config.generator, m_content, 1, {}, QcGeneratorVersion::current));
        QVERIFY(m_host->startError().isEmpty());

        auto link = std::make_unique<QcLatencyTransport>(m_host->connectClient(), delayMs, jitterMs, static_cast<quint32>(delayMs * 7 + jitterMs));
        m_session = std::make_unique<QcClientSession>(std::move(link), u"Player"_s);
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);

        m_session->sendChat(u"/gamerule mobSpawning false"_s);
        m_session->sendChat(u"/give qtcraft:bow 1"_s);
        m_session->sendChat(u"/give qtcraft:arrow 16"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->inventory().slot(1).count == 16, timeoutMs);
        m_session->resetCorrections();
    }

    // Runs `ticks` client ticks walking on the spot's yaw, with `sprint`.
    void walk(int ticks, bool sprint = false)
    {
        m_ticks = 0;
        m_session->setIntent({.forward = 1.0f, .sprint = sprint, .yaw = 90.0f});
        QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::ticked, this, [this] { ++m_ticks; });
        const bool finished = QTest::qWaitFor([&] { return m_ticks >= ticks; }, timeoutMs);
        disconnect(connection);
        QVERIFY2(finished, qPrintable(u"%1 of %2 ticks"_s.arg(m_ticks).arg(ticks)));
    }

    // Waits until the server answered the inputs sent so far.
    void settle()
    {
        const quint32 last = m_session->prediction().lastSequence() + mirrorLag;
        QTRY_VERIFY_WITH_TIMEOUT(m_session->prediction().lastAcknowledged() >= last, timeoutMs);
    }

    // Stops and waits for the answers of the server to the inputs on their way.
    void stopAndSettle()
    {
        m_session->setIntent({.yaw = 90.0f});
        settle();
    }

    double speed() const
    {
        const QcVec3 velocity = m_session->playerMotion().velocity;
        return std::hypot(velocity.x, velocity.z);
    }

    bool drawing() const { return m_session->hand().drawProgress(0.0) >= 0.0; }

    void verifySpeed(double wanted) const
    {
        QVERIFY2(std::abs(speed() - wanted) < wanted * speedTolerance, qPrintable(u"speed %1, wanted %2"_s.arg(speed()).arg(wanted)));
    }

    void verifyNoCorrection() const
    {
        const double largest = m_session->prediction().largestCorrection();
        QVERIFY2(largest == 0.0, qPrintable(u"corrected by %1 m"_s.arg(largest)));
    }

    static constexpr double drawnSpeed = QcPhysics::walkSpeed * QcPhysics::usingItemFactor;

private slots:
    void cleanup()
    {
        m_session.reset();
        if (m_host) {
            std::ignore = m_host->shutdown();
        }
        m_host.reset();
    }

    // Over a walked distance while drawing the client and the server agree on every input.
    void aWalkWhileDrawingNeedsNoCorrection()
    {
        start();
        walk(walkTicks, true);
        verifySpeed(QcPhysics::sprintSpeed);

        QVERIFY(m_session->pressUse());
        QVERIFY(drawing());
        const double startX = m_session->playerMotion().position.x;
        walk(walkTicks, true);
        verifySpeed(drawnSpeed);
        QVERIFY(m_session->intent().usingItem);
        const double walked = m_session->playerMotion().position.x - startX;
        // A second and a half at a fifth of the walk, after the first ticks of braking.
        QVERIFY2(walked > 1.0 && walked < 3.0, qPrintable(u"walked %1 m"_s.arg(walked)));

        stopAndSettle();
        verifyNoCorrection();
        QVERIFY(drawing());
    }

    void theReleaseAndTheCancelNeedNoCorrection()
    {
        start();
        walk(10);
        QVERIFY(m_session->pressUse());
        walk(walkTicks);
        verifySpeed(drawnSpeed);
        m_session->releaseUse();
        walk(walkTicks);
        verifySpeed(QcPhysics::walkSpeed);
        QVERIFY(!m_session->intent().usingItem);

        QVERIFY(m_session->pressUse());
        walk(walkTicks);
        verifySpeed(drawnSpeed);
        m_session->cancelUse();
        walk(walkTicks);
        verifySpeed(QcPhysics::walkSpeed);

        stopAndSettle();
        verifyNoCorrection();
    }

    void aSwitchedSlotNeedsNoCorrection()
    {
        start();
        walk(10);
        QVERIFY(m_session->pressUse());
        walk(walkTicks);
        verifySpeed(drawnSpeed);

        // The client's own cancel ends the slowdown, before any answer of the server could.
        m_session->selectHotbarSlot(1);
        walk(ticksBelowRoundTrip);
        QVERIFY(!drawing());
        QVERIFY(!m_session->intent().usingItem);

        walk(walkTicks);
        verifySpeed(QcPhysics::walkSpeed);

        stopAndSettle();
        verifyNoCorrection();
    }

    // The arrows leave for the server first: the client still shows them and draws, the server
    // refuses. The client's inputs move alike on both sides until it hears, then it lets go.
    void aRefusedDrawNeedsNoCorrectionAndLeavesNoSlowdown()
    {
        start();
        walk(10);
        m_session->selectHotbarSlot(1);
        QVERIFY(m_session->dropItem(true));
        m_session->selectHotbarSlot(0);
        QVERIFY(m_session->pressUse());
        QVERIFY(drawing());

        walk(walkTicks);
        QVERIFY(!drawing());
        QVERIFY(!m_session->intent().usingItem);
        verifySpeed(QcPhysics::walkSpeed);

        stopAndSettle();
        verifyNoCorrection();
    }

    void aDeathLeavesNoSlowdown()
    {
        start();
        QVERIFY(m_session->pressUse());
        walk(10);
        QVERIFY(drawing());

        // The draw ends with the news of the death itself, not with a later PlayerState.
        const QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::healthChanged, this, [this] {
            if (m_session->isDead() && !m_drawingAtDeath) {
                m_drawingAtDeath = drawing();
            }
        });
        m_session->sendChat(u"/kill"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->isDead(), timeoutMs);
        disconnect(connection);
        QCOMPARE(m_drawingAtDeath, std::optional<bool>(false));
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->intent().usingItem, timeoutMs);

        m_session->respawn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->isDead(), timeoutMs);
        settle();
        m_session->resetCorrections();
        walk(walkTicks);
        verifySpeed(QcPhysics::walkSpeed);
        stopAndSettle();
        verifyNoCorrection();
    }
};

QTEST_GUILESS_MAIN(TestDrawClient)
#include "test_draw_client.moc"
