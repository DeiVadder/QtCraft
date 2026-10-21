#include "clientcore/control/QcControlReports.h"
#include "clientcore/session/QcClientSession.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <QJsonArray>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

} // namespace

// A backpack opened from the hand as the control interface reports it (decision E-833): its
// hotbar slot instead of a position, since a held screen has no place in the world.
class TestBackpackControl : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;

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

    void aHeldBackpackReportsItsSlot()
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.seed = 1;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        config.operators = {u"Player"_s};
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, QcWorldGenerators::create(config.generator, m_content, 1));
        QVERIFY(m_host->startError().isEmpty());
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Player"_s);
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);

        // Straight up into the sky: nothing aimed at, so the backpack opens in the hand.
        QcPlayerInput look;
        look.pitch = 90.0f;
        m_session->setIntent(look);
        m_session->sendChat(u"/give backpack"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->inventory().slot(0).item == *m_content->itemId(u"qtcraft:backpack"_s), timeoutMs);
        m_session->selectHotbarSlot(0);
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->hand().target(), timeoutMs);
        QVERIFY(m_session->useItem());
        QTRY_COMPARE_WITH_TIMEOUT(m_session->machine().heldSlot(), 0, timeoutMs);

        const QJsonObject report = QcControlReports(*m_session, {}).machineJson();
        QCOMPARE(report.value(u"open"_s).toBool(), true);
        QCOMPARE(report.value(u"chest"_s).toBool(), true);
        QCOMPARE(report.value(u"heldSlot"_s).toInt(-1), 0);
        QVERIFY(!report.contains(u"position"_s));
        QCOMPARE(report.value(u"slots"_s).toArray().size(), 9);
    }
};

QTEST_GUILESS_MAIN(TestBackpackControl)
#include "test_backpack_control.moc"
