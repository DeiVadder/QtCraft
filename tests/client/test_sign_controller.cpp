#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcSignController.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "QcRawClient.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QTest>

#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;
using Overlay = QcScreenState::Overlay;

namespace {

constexpr int timeoutMs = 30'000;

} // namespace

// The sign's editor from the controller to the server (decision E-928, pattern 1 of
// docs/UI-Leitfaden.md 5): the lines go into the draft as they are typed (QcSignController::edit),
// and a screen that replaces the editor sends them; another player sees the typed text.
class TestSignController : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcSessionParts m_parts;

    QcClientSession &session() const { return *m_parts.session; }

    // The player's feet, once he stands.
    QcBlockPos feet()
    {
        const QcVec3 position = session().playerMotion().position;
        return {static_cast<int>(std::floor(position.x)), static_cast<int>(std::floor(position.y)),
                static_cast<int>(std::floor(position.z))};
    }

private slots:
    void cleanup()
    {
        m_parts.session.reset();
        if (m_parts.server) {
            std::ignore = m_parts.server->shutdown();
            m_parts.server.reset();
        }
    }

    void typedLinesGoOutWhenAnotherScreenOpens()
    {
        QcServerConfig config;
        config.viewDistance = 1;
        config.autosaveSeconds = 0;
        config.operators = {u"Writer"_s};
        m_parts.server = std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_parts.server->startError().isEmpty());
        m_parts.session = std::make_unique<QcClientSession>(m_parts.server->connectClient(), u"Writer"_s);
        session().start();
        QTRY_VERIFY_WITH_TIMEOUT(session().state() == QcClientSession::State::Playing && session().playerMotion().onGround, timeoutMs);
        QcRawClient guest(m_parts.server->connectClient());
        guest.hello(u"Guest"_s);
        QTRY_VERIFY_WITH_TIMEOUT(session().players().size() == 2, timeoutMs);

        // A sign one block north of the feet, the way to it clear; the player looks down at it.
        const QcBlockPos at = feet();
        const QcBlockPos sign = at.offset(0, 0, -1);
        session().sendChat(u"/setblock %1 %2 %3 air"_s.arg(sign.x).arg(sign.y + 1).arg(sign.z));
        // Something to stand on: a sign without support drops.
        session().sendChat(u"/setblock %1 %2 %3 stone"_s.arg(sign.x).arg(sign.y - 1).arg(sign.z));
        session().sendChat(u"/setblock %1 %2 %3 qtcraft:oak_sign"_s.arg(sign.x).arg(sign.y).arg(sign.z));
        QcPlayerInput look;
        look.yaw = 0.0f;
        look.pitch = -45.0f;
        session().setIntent(look);
        QTRY_VERIFY_WITH_TIMEOUT(session().hand().target() && session().hand().target()->block == sign, timeoutMs);

        QVERIFY(session().useItem());
        QTRY_VERIFY_WITH_TIMEOUT(session().signEditor().isOpen(), timeoutMs);
        QCOMPARE(session().signEditor().pos(), sign);

        QcScreenState screens(m_parts);
        QcSignController controller(m_parts, screens);
        screens.setPhase(QcScreenState::Phase::Playing);
        screens.setOverlay(Overlay::Sign);
        controller.edit(0, u"Typed"_s);
        controller.edit(1, u"x"_s.repeated(40));
        QCOMPARE(controller.lines().value(0), u"Typed"_s);
        // A line the sign does not have is refused.
        controller.edit(controller.lineCount(), u"Too far"_s);
        guest.signTexts.clear();

        screens.setOverlay(Overlay::Inventory);
        QVERIFY(!session().signEditor().isOpen());
        QTRY_VERIFY_WITH_TIMEOUT(!guest.signTexts.isEmpty() && guest.signTexts.last().pos == sign, timeoutMs);
        QCOMPARE(guest.signTexts.last().lines[0], u"Typed"_s);
        QCOMPARE(guest.signTexts.last().lines[1], u"x"_s.repeated(controller.lineLength()));
    }
};

QTEST_GUILESS_MAIN(TestSignController)
#include "test_sign_controller.moc"
