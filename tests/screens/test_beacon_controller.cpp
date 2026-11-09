// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcWorldLibraryController.h"
#include "app/inventory/QcBeaconController.h"
#include "app/session/QcSessionParts.h"
#include "app/settings/QcSettingsController.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "common/gameplay/QcBeacon.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

} // namespace

// The beacon's screen from the controller to the server and back (decision E-1142): the choice
// shown after an opening is the opened beacon's own, taken from the first state the server sends
// for it, and the options change only with the level and the primary effect.
class TestBeaconController : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    QcSessionParts m_parts;

    QcClientSession &session() const { return *m_parts.session; }

    QcBlockPos feet() const
    {
        const QcVec3 position = session().playerMotion().position;
        return {static_cast<int>(std::floor(position.x)), static_cast<int>(std::floor(position.y)),
                static_cast<int>(std::floor(position.z))};
    }

    void command(const QString &line) const { session().sendChat(line); }

    // A beacon at `at` on a pyramid of one layer of iron, the sky above it open.
    void buildBeacon(QcBlockPos at) const
    {
        command(u"/fill %1 %2 %3 %4 %5 %6 iron_block"_s.arg(at.x - 1).arg(at.y - 1).arg(at.z - 1).arg(at.x + 1).arg(at.y - 1).arg(at.z + 1));
        command(u"/fill %1 %2 %3 %1 %4 %3 air"_s.arg(at.x).arg(at.y + 1).arg(at.z).arg(at.y + 40));
        command(u"/setblock %1 %2 %3 beacon"_s.arg(at.x).arg(at.y).arg(at.z));
    }

    // Opens the beacon the player aims at and waits for its state.
    void openAimed(QcBlockPos beacon) const
    {
        QTRY_VERIFY_WITH_TIMEOUT(session().hand().target() && session().hand().target()->block == beacon, timeoutMs);
        QVERIFY(session().useItem());
        QTRY_VERIFY_WITH_TIMEOUT(session().beacon().isOpen() && session().beacon().pos() == beacon, timeoutMs);
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

    void theChoiceFollowsTheOpenedBeacon()
    {
        QcServerConfig config;
        config.viewDistance = 1;
        config.autosaveSeconds = 0;
        config.operators = {u"Builder"_s};
        m_parts.server = std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_parts.server->startError().isEmpty());
        m_parts.session = std::make_unique<QcClientSession>(m_parts.server->connectClient(), u"Builder"_s);
        session().start();
        QTRY_VERIFY_WITH_TIMEOUT(session().state() == QcClientSession::State::Playing && session().playerMotion().onGround, timeoutMs);

        QcWorldLibraryController library;
        QcSettingsController settings(m_parts, library, nullptr);
        QcBeaconController controller(m_parts, settings);
        connect(&session().beacon(), &QcClientBeacon::opened, &controller, &QcBeaconController::opened);
        connect(&session().beacon(), &QcClientBeacon::changed, &controller, &QcBeaconController::update);
        connect(&session().beacon(), &QcClientBeacon::closed, &controller, &QcBeaconController::update);
        QCOMPARE(controller.maxLevel(), QcBeacon::maxLevel);
        QVERIFY2(controller.paymentItems().contains(u"Iron ingot"_s), qPrintable(controller.paymentItems()));

        // A beacon one block north of the feet; the player looks down at it and pays with iron.
        const QcBlockPos beacon = feet().offset(0, 0, -1);
        buildBeacon(beacon);
        command(u"/give iron_ingot 2"_s);
        QcPlayerInput look;
        look.pitch = -45.0f;
        session().setIntent(look);
        openAimed(beacon);
        QTRY_COMPARE_WITH_TIMEOUT(controller.level(), 1, timeoutMs);
        QVERIFY(controller.primary().isEmpty());

        // The options do not change with the payment, only with the level and the primary effect.
        QSignalSpy options(&controller, &QcBeaconController::optionsChanged);
        session().inventoryMirror().moveItem(0, QcMsg::MoveInventoryItem::machineInputSlot);
        QTRY_VERIFY_WITH_TIMEOUT(controller.hasPayment(), timeoutMs);
        QCOMPARE(options.count(), 0);
        controller.choosePrimary(u"qtcraft:speed"_s);
        QCOMPARE(options.count(), 1);
        QVERIFY(controller.canConfirm());
        controller.confirm();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.hasPayment(), timeoutMs);
        session().beacon().close();
        QVERIFY(!controller.isOpen());
        QVERIFY(controller.primary().isEmpty());

        // Opened again, the screen shows what the beacon gives.
        openAimed(beacon);
        QTRY_COMPARE_WITH_TIMEOUT(controller.primary(), u"qtcraft:speed"_s, timeoutMs);
        session().beacon().close();

        // A new beacon in its place gives nothing yet: the old choice does not stay.
        command(u"/setblock %1 %2 %3 air"_s.arg(beacon.x).arg(beacon.y).arg(beacon.z));
        command(u"/setblock %1 %2 %3 beacon"_s.arg(beacon.x).arg(beacon.y).arg(beacon.z));
        openAimed(beacon);
        QVERIFY(controller.isOpen());
        QVERIFY(controller.primary().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBeaconController)
#include "test_beacon_controller.moc"
