#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "QcLatencyTransport.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

} // namespace

// The client side of the adventure mode (decision E-735) over a link with latency: the client
// refuses what the server would refuse with the same rule (no mining, no cracks, no requests, a
// rate-limited report for the HUD), takes the allow-lists the server sends with the mode and then
// breaks and places like in survival, without any correction.
class TestAdventureClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;

    void start()
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.seed = 1;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        config.defaultGameMode = QcGameMode::Adventure;
        config.operators = {u"Player"_s};
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, QcWorldGenerators::create(config.generator, m_content, 1));
        QVERIFY(m_host->startError().isEmpty());

        auto link = std::make_unique<QcLatencyTransport>(m_host->connectClient(), 50, 25, 1234u);
        m_session = std::make_unique<QcClientSession>(std::move(link), u"Player"_s);
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
        QCOMPARE(m_session->gameMode().mode(), QcGameMode::Adventure);
        QTest::qWait(400);
        m_session->resetCorrections();

        QcPlayerInput look;
        look.pitch = -90.0f;
        m_session->setIntent(look);
    }

    QcBlockPos below() const { return QcPhysics::blockContaining(m_session->playerMotion().position).offset(0, -1, 0); }

    // Waits for `ticks` client ticks.
    void waitTicks(quint64 ticks)
    {
        const quint64 until = m_session->tickCount() + ticks;
        QTRY_VERIFY_WITH_TIMEOUT(m_session->tickCount() >= until, timeoutMs);
    }

private slots:
    void cleanup()
    {
        if (m_session) {
            m_session->disconnectFromServer();
            m_session.reset();
        }
        if (m_host) {
            QVERIFY(m_host->shutdown().has_value());
            m_host.reset();
        }
    }

    void miningIsRefusedUntilTheListAllowsIt()
    {
        start();
        const QcBlockPos ground = below();
        const std::optional<QcBlockId> before = m_session->world()->blockAt(ground);
        QVERIFY(before && *before != qcAirBlockId);

        QSignalSpy refused(m_session.get(), &QcClientSession::actionRefused);
        const quint64 startTick = m_session->tickCount();
        m_session->setMining(true);
        waitTicks(60);
        // Nothing was predicted that the server would have had to correct.
        QVERIFY(m_session->prediction().largestCorrection() < 1e-9);
        QVERIFY(!m_session->hand().isMining());
        QCOMPARE(m_session->hand().miningProgress(), 0.0);
        QCOMPARE(m_session->world()->blockAt(ground), before);
        // Reported again while the button is held, but at most once per refusalReportTicks.
        const quint64 held = m_session->tickCount() - startTick;
        QVERIFY(refused.count() >= 1);
        QVERIFY(quint64(refused.count()) <= held / QcClientSession::refusalReportTicks + 1);
        m_session->setMining(false);

        const QString name = m_session->world()->content().block(*before).name;
        m_session->sendChat(u"/adventure canBreak * "_s + name);
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->gameMode().adventure().isEmpty(), timeoutMs);
        m_session->setMining(true);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->world()->blockAt(ground) == std::optional<QcBlockId>(qcAirBlockId), timeoutMs);
        m_session->setMining(false);
    }

    void placingIsRefusedUntilTheListAllowsIt()
    {
        start();
        // An empty hand on a block that does not react does nothing: no request, no notice.
        {
            QSignalSpy quiet(m_session.get(), &QcClientSession::actionRefused);
            waitTicks(2);
            QVERIFY(m_session->hand().target().has_value());
            QVERIFY(!m_session->useItem());
            QCOMPARE(quiet.count(), 0);
        }

        m_session->sendChat(u"/give stone 2"_s);
        const QcItemId stone = *m_content->itemId(u"qtcraft:stone"_s);
        QTRY_COMPARE_WITH_TIMEOUT(m_session->inventory().selectedStack().item, stone, timeoutMs);

        // Looking down from above, the use would place stone next to the ground block's top face,
        // where the player stands; aim at a block beside the feet instead.
        QcPlayerInput look;
        look.pitch = -60.0f;
        m_session->setIntent(look);
        waitTicks(2);
        QVERIFY(m_session->hand().target().has_value());
        const QcBlockPos aimed = m_session->hand().target()->block;

        QSignalSpy refused(m_session.get(), &QcClientSession::actionRefused);
        QVERIFY(!m_session->useItem());
        QCOMPARE(refused.count(), 1);
        waitTicks(10);
        QCOMPARE(m_session->inventory().selectedStack().count, 2);

        const QString aimedName = m_session->world()->content().block(*m_session->world()->blockAt(aimed)).name;
        m_session->sendChat(u"/adventure canPlaceOn stone "_s + aimedName);
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->gameMode().adventure().isEmpty(), timeoutMs);
        QVERIFY(m_session->useItem());
        QTRY_COMPARE_WITH_TIMEOUT(m_session->inventory().selectedStack().count, 1, timeoutMs);
        QVERIFY(m_session->prediction().largestCorrection() < 1e-9);
    }
};

QTEST_MAIN(TestAdventureClient)
#include "test_adventure_client.moc"
