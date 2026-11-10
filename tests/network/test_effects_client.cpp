// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientSession.h"
#include "common/content/items/QcEffectDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcEffectRules.h"
#include "common/physics/QcPhysics.h"
#include "fixtures/QcFlatLocalHost.h"
#include "fixtures/QcLatencyTransport.h"
#include "fixtures/QcTestUtil.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;
constexpr int delayMs = 100;
constexpr int jitterMs = 50;
// The speed a walk reaches is this close to the wanted one.
constexpr double speedTolerance = 0.05;

} // namespace

// The prediction of a player whose status effects change across a link with a round trip of 200
// to 300 ms (decision E-992): the client hears of an effect late, and its inputs in flight quote
// the count of the effects they were predicted with, so speed, jump boost and slow falling start
// and end without any correction. Another player sees an invisible one as invisible.
class TestEffectsClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    int m_ticks = 0;
    // The highest the feet rose above the floor while a jump is watched.
    double m_highest = 0.0;

    void start()
    {
        m_host = qcFlatSurvivalHost(m_content);
        QVERIFY(m_host->startError().isEmpty());
        m_session = session(u"Player"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
        m_session->sendChat(u"/gamerule mobSpawning false"_s);
        m_session->resetCorrections();
    }

    std::unique_ptr<QcClientSession> session(const QString &name)
    {
        auto link = std::make_unique<QcLatencyTransport>(m_host->connectClient(), delayMs, jitterMs, static_cast<quint32>(delayMs * 7 + jitterMs));
        auto started = std::make_unique<QcClientSession>(std::move(link), name);
        started->start();
        return started;
    }

    // Runs `ticks` client ticks with `intent`.
    void run(int ticks, const QcPlayerInput &intent)
    {
        m_ticks = 0;
        m_session->setIntent(intent);
        QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::ticked, this, [this] { ++m_ticks; });
        const bool finished = QTest::qWaitFor([&] { return m_ticks >= ticks; }, timeoutMs);
        disconnect(connection);
        QVERIFY(finished);
    }

    // Runs with `intent` until the session knows of effects or of none.
    void runUntilEffects(bool wanted, const QcPlayerInput &intent)
    {
        m_session->setIntent(intent);
        QVERIFY(QTest::qWaitFor([&] { return m_session->vitals().effects().isEmpty() != wanted; }, timeoutMs));
    }

    double speed() const
    {
        const QcVec3 velocity = m_session->playerMotion().velocity;
        return std::hypot(velocity.x, velocity.z);
    }

    void verifyNoCorrection() const
    {
        const double largest = m_session->prediction().largestCorrection();
        QVERIFY2(largest == 0.0, qPrintable(u"corrected by %1 m"_s.arg(largest)));
    }

private slots:
    void cleanup()
    {
        m_session.reset();
        if (m_host) {
            std::ignore = m_host->shutdown();
        }
        m_host.reset();
    }

    // Starts and ends while walking: both changes reach the client a round trip late.
    void speedStartsAndEndsWithoutACorrection()
    {
        start();
        const QcPlayerInput walking{.forward = 1.0f, .yaw = 90.0f};
        run(20, walking);
        m_session->sendChat(u"/effect give Player speed 2 2"_s);
        runUntilEffects(true, walking);
        run(20, walking);
        const double wanted = QcPhysics::walkSpeed * 1.4;
        QVERIFY2(std::abs(speed() - wanted) < wanted * speedTolerance, qPrintable(u"speed %1, wanted %2"_s.arg(speed()).arg(wanted)));
        runUntilEffects(false, walking);
        run(20, walking);
        QVERIFY2(std::abs(speed() - QcPhysics::walkSpeed) < QcPhysics::walkSpeed * speedTolerance, qPrintable(u"speed %1"_s.arg(speed())));
        verifyNoCorrection();
    }

    // Jumps all the time, so that many inputs of the change are jumps.
    void jumpBoostStartsAndEndsWithoutACorrection()
    {
        start();
        const QcPlayerInput jumping{.jump = true};
        run(20, jumping);
        m_session->sendChat(u"/effect give Player jump_boost 2 3"_s);
        runUntilEffects(true, jumping);
        m_highest = 0.0;
        const double floor = std::floor(m_session->playerMotion().position.y);
        m_ticks = 0;
        QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::ticked, this, [this, floor] {
            ++m_ticks;
            m_highest = std::max(m_highest, m_session->playerMotion().position.y - floor);
        });
        QVERIFY(QTest::qWaitFor([&] { return m_ticks >= 30; }, timeoutMs));
        disconnect(connection);
        QVERIFY2(m_highest > 2.0, qPrintable(u"%1 m"_s.arg(m_highest)));
        runUntilEffects(false, jumping);
        run(30, jumping);
        verifyNoCorrection();
    }

    // Begins in the middle of a fall and ends in it.
    void slowFallingStartsAndEndsWithoutACorrection()
    {
        start();
        const QcVec3 at = m_session->playerMotion().position;
        m_session->sendChat(u"/tp Player %1 %2 %3"_s.arg(at.x).arg(at.y + 60.0).arg(at.z));
        QVERIFY(QTest::qWaitFor([&] { return m_session->playerMotion().position.y > at.y + 30.0; }, timeoutMs));
        m_session->resetCorrections();
        m_session->sendChat(u"/effect give Player slow_falling 2"_s);
        runUntilEffects(true, {});
        run(10, {});
        QVERIFY2(m_session->playerMotion().velocity.y >= -2.0, qPrintable(u"%1 m/s"_s.arg(m_session->playerMotion().velocity.y)));
        runUntilEffects(false, {});
        run(10, {});
        QVERIFY2(m_session->playerMotion().velocity.y < -3.0, qPrintable(u"%1 m/s"_s.arg(m_session->playerMotion().velocity.y)));
        verifyNoCorrection();
    }

    // The client draws the golden hearts from what the content it was sent says of the kind.
    void theClientKnowsWhatAbsorptionGives()
    {
        start();
        const QcEffectDefinition *kind = m_session->content()->effect(u"qtcraft:absorption"_s);
        QVERIFY(kind);
        QCOMPARE(kind->absorption, 4.0);
    }

    void anotherPlayerSeesAnInvisibleOneAsInvisible()
    {
        start();
        std::unique_ptr<QcClientSession> other = session(u"Other"_s);
        QTRY_VERIFY_WITH_TIMEOUT(other->state() == QcClientSession::State::Playing, timeoutMs);
        const auto seen = [&](bool invisible) {
            // The other client sees one player, the first.
            const QHash<quint32, QcClientEntity> &entities = other->entities();
            return std::ranges::any_of(entities, [invisible](const QcClientEntity &entity) {
                return entity.kind == QcEntityKind::Player && ((entity.flags & QcMsg::Invisible) != 0) == invisible;
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT(seen(false), timeoutMs);
        m_session->sendChat(u"/effect give Player invisibility 1"_s);
        QTRY_VERIFY_WITH_TIMEOUT(seen(true), timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(seen(false), timeoutMs);
    }
};

QTEST_GUILESS_MAIN(TestEffectsClient)
#include "test_effects_client.moc"
