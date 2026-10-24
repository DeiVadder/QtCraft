#include "app/session/QcSessionAudio.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QTest>

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30000;

bool near(const QcVec3 &a, const QcVec3 &b)
{
    return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9 && std::abs(a.z - b.z) < 1e-9;
}

} // namespace

// The sounds of a session reach the audio engine as QcSessionWiring connects them (decision
// E-876): every client tick places the listener at the player's eyes, looking the way the screen
// shows, so playing positional voices follow the player. A session against an in-process server,
// ticked by hand; the engine stays without an output device.
class TestSessionAudio : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QcLocalServerHost> m_server;
    std::unique_ptr<QcClientSession> m_session;

private slots:
    void initTestCase()
    {
        std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"SessionAudio"_s;
        m_server = std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        m_session = std::make_unique<QcClientSession>(m_server->connectClient(), u"Ears"_s);
        m_session->start(QcClientSession::TickMode::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing && m_session->world(), timeoutMs);
    }

    void cleanupTestCase()
    {
        m_session.reset();
        QVERIFY(m_server->shutdown().has_value());
        m_server.reset();
    }

    void eachTickMovesTheListener()
    {
        QcAudioEngine engine;
        QcSessionAudio audio(engine);
        audio.connectSession(*m_session);
        // Before the first tick nothing placed it.
        QCOMPARE(engine.listener().yawDegrees, 0.0);

        QcPlayerInput intent = m_session->intent();
        intent.yaw = 37.0f;
        m_session->setIntent(intent);
        m_session->tick();
        const QcVec3 eyes = m_session->playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
        QVERIFY(near(engine.listener().ears, eyes));
        QCOMPARE(engine.listener().yawDegrees, 37.0);

        // Turned and in the front view, which looks back at the player: left and right swap.
        intent.yaw = 90.0f;
        m_session->setIntent(intent);
        m_session->setCameraMode(QcCameraMode::Front);
        m_session->tick();
        QCOMPARE(engine.listener().yawDegrees, 270.0);
        m_session->setCameraMode(QcCameraMode::FirstPerson);
        QCOMPARE(QcSessionAudio::listenerOf(*m_session).yawDegrees, 90.0);
    }
};

QTEST_MAIN(TestSessionAudio)
#include "test_session_audio.moc"
