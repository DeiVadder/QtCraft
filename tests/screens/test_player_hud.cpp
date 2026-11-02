// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcPlayerController.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {
constexpr int timeoutMs = 30000;
} // namespace

// The status rows of the HUD with a session against an in-process server (row 79.63): which game
// modes show them (PlayerController.statusShown and the rows' `shown`), the icons' fills, the food
// icon, the status effects' line and the refusal notice's text. The pure rules: test_hud_rules.
class TestPlayerHud : public QObject
{
    Q_OBJECT

private:
    QcSessionParts m_parts;
    std::unique_ptr<QcWorldLibraryController> m_library;
    std::unique_ptr<QcSettingsController> m_settings;
    std::unique_ptr<QcPlayerController> m_player;

    QcClientSession &session() const { return *m_parts.session; }

    // Sends the command and waits until the server's answer shows in `done`.
    void command(const QString &line, const std::function<bool()> &done)
    {
        session().sendChat(line);
        QTRY_VERIFY_WITH_TIMEOUT(done(), timeoutMs);
    }

    void gameMode(QcGameMode mode)
    {
        command(u"/gamemode "_s + QcGameModes::name(mode), [this, mode] { return session().gameMode().mode() == mode; });
    }

    // The slot of the first stack of `item`, -1 for none.
    int slotOf(const QString &item) const
    {
        const auto content = session().content();
        for (int slot = 0; slot < QcInventory::slotCount; ++slot) {
            const QcItemStack &stack = session().inventory().slot(slot);
            if (!stack.isEmpty() && content->item(stack.item).name == item) {
                return slot;
            }
        }
        return -1;
    }

private slots:
    void initTestCase()
    {
        std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"PlayerHud"_s;
        config.operators = {u"Hud"_s};
        m_parts.server = std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        m_parts.session = std::make_unique<QcClientSession>(m_parts.server->connectClient(), u"Hud"_s);
        session().start();
        QTRY_VERIFY_WITH_TIMEOUT(session().state() == QcClientSession::State::Playing && session().world(), timeoutMs);

        m_library = std::make_unique<QcWorldLibraryController>();
        m_settings = std::make_unique<QcSettingsController>(m_parts, *m_library, nullptr);
        m_player = std::make_unique<QcPlayerController>(m_parts, *m_settings);
        // As QcSessionWiring connects the player.
        const QcPlayerController *player = m_player.get();
        connect(&session(), &QcClientSession::inventoryChanged, player, &QcPlayerController::updateInventory);
        connect(&session(), &QcClientSession::healthChanged, player, &QcPlayerController::healthChanged);
        connect(&session(), &QcClientSession::foodChanged, player, &QcPlayerController::foodChanged);
        connect(&session(), &QcClientSession::airChanged, player, &QcPlayerController::airChanged);
        connect(&session(), &QcClientSession::gameModeChanged, player, &QcPlayerController::gameModeChanged);
        connect(&session(), &QcClientSession::actionRefused, player, &QcPlayerController::reportRefusal);
        m_player->useContent();
    }

    void cleanupTestCase()
    {
        m_player.reset();
        m_parts.session.reset();
        QVERIFY(m_parts.server->shutdown().has_value());
        m_parts.server.reset();
    }

    // Survival and adventure show hearts, food and experience; creative and spectator none of the
    // status rows. Armour and air need more (worn armour, the eyes under water).
    void statusRowsFollowTheGameMode()
    {
        using Mode = QcGameMode;
        for (const Mode mode : {Mode::Creative, Mode::Survival, Mode::Spectator, Mode::Adventure}) {
            gameMode(mode);
            const bool shown = mode == Mode::Survival || mode == Mode::Adventure;
            QCOMPARE(m_player->statusShown(), shown);
            QCOMPARE(m_player->heartRow()->shown(), shown);
            QCOMPARE(m_player->foodRow()->shown(), shown);
            QVERIFY(!m_player->armourRow()->shown());
            QVERIFY(!m_player->airRow()->shown());
        }
        gameMode(Mode::Survival);
    }

    // Full health and food fill all ten icons; the air bubbles are full and hidden on land.
    void rowsFillFromTheVitals()
    {
        QTRY_COMPARE_WITH_TIMEOUT(m_player->heartRow()->iconFills(), QList<double>(10, 1.0), timeoutMs);
        QCOMPARE(m_player->foodRow()->iconFills(), QList<double>(10, 1.0));
        QCOMPARE(m_player->airRow()->iconFills(), QList<double>(10, 1.0));
        QVERIFY(m_player->armourRow()->iconFills().size() == 10);
        QCOMPARE(m_player->armourRow()->iconFills(), QList<double>(10, 0.0));
        QCOMPARE(m_player->foodIcon(), u"qtcraft:cooked_porkchop"_s);
    }

    // Worn armour shows its row with its points, two per icon, only where the status rows show.
    void armourShowsWhileWorn()
    {
        command(u"/give qtcraft:iron_chestplate"_s, [this] { return slotOf(u"qtcraft:iron_chestplate"_s) >= 0; });
        const int chest = QcInventoryModel::equipmentFirstRow + static_cast<int>(QcEquipmentSlot::Chest);
        m_player->moveItem(slotOf(u"qtcraft:iron_chestplate"_s), chest, 0);
        QTRY_VERIFY_WITH_TIMEOUT(m_player->armour() > 0, timeoutMs);
        QVERIFY(m_player->armourRow()->shown());
        QCOMPARE(m_player->armourRow()->iconFills(), QcPointsRow::fills(m_player->armour(), QcPlayerController::maxArmour(), 2));
        QCOMPARE(m_player->armourRow()->iconFills().first(), 1.0);

        gameMode(QcGameMode::Creative);
        QVERIFY(!m_player->armourRow()->shown());
        gameMode(QcGameMode::Survival);
        QVERIFY(m_player->armourRow()->shown());
    }

    // The effect's line: its name, the level in Roman numerals from II on, the time left as m:ss.
    void effectLine()
    {
        command(u"/effect give Hud slowness 65 2"_s, [this] { return !m_player->effects().isEmpty(); });
        const QVariantMap effect = m_player->effects().first().toMap();
        const QString text = effect.value(u"text"_s).toString();
        QVERIFY2(QRegularExpression(u"^\\S.* II  1:0\\d$"_s).match(text).hasMatch(), qPrintable(text));
        QVERIFY(text.startsWith(effect.value(u"label"_s).toString() + u" II"_s));

        command(u"/effect clear Hud"_s, [this] { return m_player->effects().isEmpty(); });
        command(u"/effect give Hud slowness 5 1"_s, [this] { return !m_player->effects().isEmpty(); });
        const QString single = m_player->effects().first().toMap().value(u"text"_s).toString();
        QVERIFY2(QRegularExpression(u"^\\S.*\\S  0:0\\d$"_s).match(single).hasMatch(), qPrintable(single));
        QVERIFY(!single.contains(u" I "_s));
        command(u"/effect clear Hud"_s, [this] { return m_player->effects().isEmpty(); });
    }

    // The refusal notice names adventure mode only in adventure mode.
    void refusalSaysWhy()
    {
        QSignalSpy refused(m_player.get(), &QcPlayerController::actionRefused);
        gameMode(QcGameMode::Adventure);
        m_player->reportRefusal();
        QCOMPARE(refused.last().first().toString(), u"Adventure mode: not allowed here"_s);
        gameMode(QcGameMode::Survival);
        m_player->reportRefusal();
        QCOMPARE(refused.last().first().toString(), u"Not allowed here"_s);
    }

    // The furnace screen's handbook key opens the page of the machine that is open: a smoker's.
    void machineBlockNamesTheOpenMachine()
    {
        QCOMPARE(m_player->machineBlock(), QString());
        connect(&session(), &QcClientSession::machineOpened, m_player.get(), &QcPlayerController::nameOpenMachine);
        connect(&session(), &QcClientSession::machineChanged, m_player.get(), &QcPlayerController::updateMachine);
        QcPlayerInput look = session().intent();
        look.pitch = -90.0f;
        session().setIntent(look);
        command(u"/setblock ~ ~-1 ~ qtcraft:smoker"_s, [this] {
            const std::optional<QcRaycastHit> &target = session().hand().target();
            const std::optional<QcBlockId> block = target ? session().world()->blockAt(target->block) : std::nullopt;
            return block && session().content()->block(*block).name == u"qtcraft:smoker"_s;
        });
        // A use may fall between two targets: pressed again until the screen opens.
        QTRY_VERIFY_WITH_TIMEOUT(session().machine().isOpen() || (session().pressUse() && false), timeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(m_player->machineBlock(), u"qtcraft:smoker"_s, timeoutMs);
        session().closeMachine();
    }

    // A dead player's hearts stay shown (behind the death screen), all empty.
    void deadPlayerShowsEmptyHearts()
    {
        command(u"/kill"_s, [this] { return m_player->dead(); });
        QTRY_COMPARE_WITH_TIMEOUT(m_player->heartRow()->iconFills(), QList<double>(10, 0.0), timeoutMs);
        QVERIFY(m_player->heartRow()->shown());
        QVERIFY(m_player->statusShown());
    }
};

QTEST_MAIN(TestPlayerHud)
#include "test_player_hud.moc"
