// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcTransport.h"
#include "fixtures/QcServerTestFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/gameplay/QcSpectatorRules.h"

#include <QTemporaryDir>

using namespace Qt::StringLiterals;

// The spectator mode on the server (decision E-730): flying through blocks the way the client
// predicts it, no harm from the world, no interaction, no body for others (invisible, marked in the
// player list, pressing no plate, picking up nothing), the commands, coming out of blocks when the
// mode ends, and what is saved.
class TestSpectatorMode : public QcServerTestFixture
{
    Q_OBJECT

private:
    static QcServerConfig savedIn(const QTemporaryDir &directory)
    {
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        return config;
    }

    // Sends `input` for `count` ticks with fresh sequence numbers.
    void move(QcPlayerInput input, int count)
    {
        for (int i = 0; i < count; ++i) {
            input.sequence = ++m_sequence;
            m_client->connection.send(QcMsg::PlayerInput{input});
            QcTestUtil::pump();
            ticks(1);
        }
    }

    // A second player on the same server, who watches Raw.
    std::unique_ptr<RawClient> joinOther()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto other = std::make_unique<RawClient>(std::move(clientEnd));
        other->hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(3);
        return other;
    }

    std::optional<QcMsg::PlayerList::Entry> listed(const RawClient &client, const QString &name) const
    {
        for (const QcMsg::PlayerList::Entry &entry : client.playerList) {
            if (entry.name == name) {
                return entry;
            }
        }
        return std::nullopt;
    }

    QcBlockPos feet() const
    {
        const QcVec3 p = m_client->motion.position;
        return {static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
    }

private slots:
    void commandsSetAndShowTheMode()
    {
        join();
        chat(u"/gamemode spectator"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Spectator);
        QCOMPARE(m_client->chatKeys.constLast().key, u"Set the game mode of %1 to spectator"_s);
        chat(u"/gamemode"_s);
        QCOMPARE(m_client->chatKeys.constLast().key, u"Game mode of %1: spectator"_s);
        chat(u"/gamemode s"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Survival);
        chat(u"/gamemode sp"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Spectator);
        chat(u"/defaultgamemode spectator"_s);
        QCOMPARE(m_client->chatKeys.constLast().key, u"New players start in spectator"_s);
    }

    void serverFliesThroughBlocksAsTheClientPredicts()
    {
        join();
        chat(u"/gamemode spectator"_s);
        idle(2);
        // A wall of stone ahead (yaw 0 looks along -z) and a ceiling above.
        const QcBlockPos start = feet();
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = 0; dy <= 6; ++dy) {
                for (int dz = -12; dz <= -2; ++dz) {
                    m_server->world().setBlock(start.offset(dx, dy, dz), id("qtcraft:stone"));
                }
            }
        }

        // The client's prediction steps the same inputs through the same physics: no correction.
        QcEntityMotion predicted = m_client->motion;
        QcPlayerInput input;
        input.forward = 1.0f;
        input.jump = true;
        input.flySpeed = 15;
        for (int i = 0; i < 30; ++i) {
            predicted.stepPlayer(input, m_server->world(), QcPhysics::tickDuration, QcMovement::NoClip);
        }
        move(input, 30);
        QVERIFY(m_client->motion.position.z < double(start.z) - 10.0);
        QVERIFY((m_client->motion.position - predicted.position).length() < 1e-9);
        QVERIFY(m_client->motion.flying);
        // Inside the stone, unhurt (no suffocation).
        idle(30);
        QCOMPARE(m_client->health, QcServerClient::maxHealth);
    }

    void survivalCannotFlyThroughBlocks()
    {
        join();
        const QcBlockPos start = feet();
        for (int dy = 0; dy <= 2; ++dy) {
            m_server->world().setBlock(start.offset(0, dy, -2), id("qtcraft:stone"));
        }
        QcPlayerInput input;
        input.forward = 1.0f;
        input.flySpeed = QcPlayerInput::maxFlySpeed;
        move(input, 20);
        QVERIFY(m_client->motion.position.z > double(start.z) - 1.0);
    }

    void theWorldDoesNotHurtASpectator()
    {
        join();
        chat(u"/gamemode spectator"_s);
        m_server->world().setBlock(feet(), id("qtcraft:lava"));
        idle(40);
        QCOMPARE(m_client->health, QcServerClient::maxHealth);
        QVERIFY(!m_client->burning);

        // Deep in the void, where survival players lose health every half second.
        chat(u"/tp ~ %1 ~"_s.arg(QcWorld::voidFloorY + 10.0));
        idle(40);
        QCOMPARE(m_client->health, QcServerClient::maxHealth);
        QVERIFY(!m_client->dead);

        // Not even /kill: a spectator would drop his survival inventory unseen.
        chat(u"/kill"_s);
        idle(2);
        QVERIFY(!m_client->dead);
    }

    void aSpectatorTouchesNothing()
    {
        join();
        chat(u"/give stone 5"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:stone"))});
        chat(u"/gamemode spectator"_s);
        // The survival inventory stays (the same set), but nothing of it can be used.
        QCOMPARE(countOf(item("qtcraft:stone")), 5);

        const QcBlockPos wall = feet().offset(0, 0, -2);
        m_server->world().setBlock(wall, id("qtcraft:dirt"));
        m_client->connection.send(QcMsg::StartMining{wall});
        send(QcMsg::FinishMining{wall});
        QCOMPARE(m_server->world().blockAt(wall), id("qtcraft:dirt"));

        send(QcMsg::UseItem{wall, QcBlockFace::Up, {0.5, 1.0, 0.5}});
        QCOMPARE(m_server->world().blockAt(wall.offset(0, 1, 0)), qcAirBlockId);
        QCOMPARE(countOf(item("qtcraft:stone")), 5);

        send(QcMsg::DropItem{false});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:stone")), 20, 5});
        send(QcMsg::CreativeItem{0, {item("qtcraft:diamond"), 1, 0}});
        QCOMPARE(countOf(item("qtcraft:stone")), 5);
        QCOMPARE(droppedCount(item("qtcraft:stone")), 0);
        QCOMPARE(countOf(item("qtcraft:diamond")), 0);

        // Choosing a hotbar slot stays allowed.
        send(QcMsg::SelectHotbarSlot{3});
        QCOMPARE(m_client->selected, 3);
    }

    void aSpectatorPicksUpNothing()
    {
        join();
        chat(u"/gamemode spectator"_s);
        const QcVec3 at = m_client->motion.position + QcVec3{0.0, 0.1, 0.0};
        m_server->items().spawn({{item("qtcraft:stick"), 3, 0}, at, {}}, m_server->entities().newId());
        idle(60);
        QCOMPARE(countOf(item("qtcraft:stick")), 0);
        QCOMPARE(droppedCount(item("qtcraft:stick")), 3);

        chat(u"/gamemode survival"_s);
        idle(10);
        QCOMPARE(countOf(item("qtcraft:stick")), 3);
    }

    void aSpectatorPressesNoPlate()
    {
        join();
        const QcBlockPos plate = feet().offset(2, 0, 0);
        const QcBlockId released = id("qtcraft:stone_pressure_plate");
        m_server->world().setBlock(plate, released);
        chat(u"/gamemode spectator"_s);
        chat(u"/tp %1 %2 %3"_s.arg(plate.x + 0.5).arg(plate.y).arg(plate.z + 0.5));
        idle(5);
        QCOMPARE(m_server->world().blockAt(plate), released);

        chat(u"/gamemode survival"_s);
        idle(5);
        QVERIFY(m_server->world().blockAt(plate) != released);
    }

    void othersNeitherSeeNorMeetASpectator()
    {
        join();
        const std::unique_ptr<RawClient> other = joinOther();
        const quint32 raw = m_client->entityId;
        QVERIFY(other->entities.contains(raw));
        QVERIFY(!listed(*other, u"Raw"_s)->spectator);

        chat(u"/gamemode spectator"_s);
        ticks(2);
        QcTestUtil::pump();
        QVERIFY(!other->entities.contains(raw));
        // Still listed, marked as spectator; the own client sees the mark too.
        QVERIFY(listed(*other, u"Raw"_s)->spectator);
        QVERIFY(listed(*m_client, u"Raw"_s)->spectator);
        // The spectator still sees the others.
        QVERIFY(m_client->entities.contains(other->entityId));

        // Moving around does not bring him back into view.
        QcPlayerInput input;
        input.strafe = 1.0f;
        move(input, 10);
        QVERIFY(!other->entities.contains(raw));

        chat(u"/gamemode creative"_s);
        ticks(2);
        QcTestUtil::pump();
        QVERIFY(other->entities.contains(raw));
        QVERIFY(!listed(*other, u"Raw"_s)->spectator);
    }

    void leavingInsideBlocksComesOutAbove()
    {
        join();
        chat(u"/gamemode spectator"_s);
        const QcBlockPos start = feet();
        for (int dy = -1; dy <= 3; ++dy) {
            m_server->world().setBlock(start.offset(0, dy, 0), id("qtcraft:stone"));
        }
        idle(2);

        chat(u"/gamemode survival"_s);
        idle(3);
        QCOMPARE(m_client->motion.position.y, double(start.y + 4));
        QVERIFY(m_client->motion.onGround);

        // Free already: the player stays where he is.
        chat(u"/gamemode spectator"_s);
        idle(2);
        const QcVec3 before = m_client->motion.position;
        chat(u"/gamemode creative"_s);
        idle(1);
        QCOMPARE(m_client->motion.position, before);
    }

    void leavingTheModeEndsTheFlightsMomentum()
    {
        join();
        chat(u"/gamemode spectator"_s);
        chat(u"/tp ~ ~10 ~"_s);
        QcPlayerInput fast;
        fast.forward = 1.0f;
        fast.sprint = true;
        move(fast, 10);
        QVERIFY(std::abs(m_client->motion.velocity.z) > 10.0);

        // Free in the air: no lift, but the speed of the flight is gone, not only slowed.
        chat(u"/gamemode creative"_s);
        idle(1);
        QCOMPARE(m_client->motion.velocity.x, 0.0);
        QCOMPARE(m_client->motion.velocity.z, 0.0);
    }

    void aSpectatorLeavesHisMinecart()
    {
        join();
        chat(u"/summon minecart"_s);
        QCOMPARE(m_server->carts().all().size(), 1);
        const quint32 cart = m_server->carts().all().constFirst().id;
        send(QcMsg::UseEntity{cart});
        idle(2);
        QCOMPARE(m_server->carts().find(cart)->rider, m_client->entityId);

        // At once with the mode, before any input.
        chat(u"/gamemode spectator"_s);
        QCOMPARE(m_server->carts().find(cart)->rider, 0u);
        idle(2);
        QCOMPARE(m_client->vehicle, 0u);
    }

    void operatorCommandsStayAllowed()
    {
        join();
        chat(u"/gamemode spectator"_s);
        const QcBlockPos pos = feet().offset(0, 0, -3);
        chat(u"/setblock %1 %2 %3 stone"_s.arg(pos.x).arg(pos.y).arg(pos.z));
        QCOMPARE(m_server->world().blockAt(pos), id("qtcraft:stone"));
        chat(u"/give diamond 2"_s);
        QCOMPARE(countOf(item("qtcraft:diamond")), 2);
    }

    void freePlaceIsTheLowestAbove()
    {
        join();
        const QcBlockPos start = feet();
        m_server->world().setBlock(start, id("qtcraft:stone"));
        m_server->world().setBlock(start.offset(0, 2, 0), id("qtcraft:stone"));
        const QcVec3 inside{start.x + 0.5, start.y + 0.3, start.z + 0.5};
        // The cell above the stone has only one free block before the next stone; the body needs two.
        QCOMPARE(QcSpectatorRules::freePlaceAbove(m_server->world(), inside), (QcVec3{inside.x, double(start.y + 3), inside.z}));
        m_server->world().setBlock(start.offset(0, 2, 0), qcAirBlockId);
        QCOMPARE(QcSpectatorRules::freePlaceAbove(m_server->world(), inside), (QcVec3{inside.x, double(start.y + 1), inside.z}));
    }

    void theModeIsSaved()
    {
        QTemporaryDir directory;
        join(savedIn(directory));
        chat(u"/gamemode spectator"_s);
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        m_server.reset();

        join(savedIn(directory));
        QCOMPARE(m_client->gameMode, QcGameMode::Spectator);
    }
};

QTEST_GUILESS_MAIN(TestSpectatorMode)
#include "test_spectator_mode.moc"
