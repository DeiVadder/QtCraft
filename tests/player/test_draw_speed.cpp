// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcPhysics.h"
#include "fixtures/QcServerTestFixture.h"
#include "server/projectiles/QcPlayerLaunchers.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

// A draw slows the player on the server (decision E-986): a fifth of the walking speed and no
// sprint, known from the draw alone, so a client that leaves the mark out of its inputs gains
// nothing; the mark of a client slows without a draw, so that a refused draw moves as predicted.
// The release, a cancel, a change of the hand, death and a draw without arrows all leave the normal
// speed, and every PlayerState tells the client how the draw stands.
class TestDrawSpeed : public QcServerTestFixture
{
    Q_OBJECT

private:
    using Action = QcMsg::DrawLauncher::Action;

    // Long enough to reach the steady speed from a stand on the floor of armedStage().
    static constexpr int walkTicks = 16;
    // The speed a walk reaches is this close to the wanted one.
    static constexpr double speedTolerance = 0.05;

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // The player in survival with a bow in slot 0 and arrows in slot 1, on a wide floor.
    void armedStage()
    {
        join();
        for (int dx = -20; dx <= 20; ++dx) {
            for (int dz = -20; dz <= 20; ++dz) {
                m_server->world().setBlock(m_feet.offset(dx, -1, dz), id("qtcraft:stone"));
                m_server->world().setBlock(m_feet.offset(dx, 0, dz), qcAirBlockId);
                m_server->world().setBlock(m_feet.offset(dx, 1, dz), qcAirBlockId);
            }
        }
        player().gameMode = QcGameMode::Survival;
        player().inventory.setSlot(0, {item("qtcraft:bow"), 1});
        player().inventory.setSlot(1, {item("qtcraft:arrow"), 16});
        player().inventory.setSelectedHotbarSlot(0);
        idle(2);
    }

    // One input into the server, stepped at once.
    void input(QcPlayerInput intent)
    {
        intent.sequence = ++m_sequence;
        m_client->connection.send(QcMsg::PlayerInput{intent});
        QcTestUtil::pump();
        ticks(1);
    }

    // Walks with `intent` from a stand and returns the speed reached.
    double speedOf(QcPlayerInput intent)
    {
        player().motion.position = {m_feet.x + 0.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        player().motion.velocity = {};
        intent.forward = 1.0f;
        for (int tick = 0; tick < walkTicks; ++tick) {
            input(intent);
        }
        const QcVec3 velocity = player().motion.velocity;
        return std::hypot(velocity.x, velocity.z);
    }

    static bool near(double speed, double wanted) { return std::abs(speed - wanted) < wanted * speedTolerance; }

    void verifyDrawnSpeed(double speed) const
    {
        QVERIFY2(near(speed, QcPhysics::walkSpeed * QcPhysics::usingItemFactor), qPrintable(u"speed %1"_s.arg(speed)));
    }

    void verifyWalkingSpeed(double speed) const
    {
        QVERIFY2(near(speed, QcPhysics::walkSpeed), qPrintable(u"speed %1"_s.arg(speed)));
    }

private slots:
    void aDrawWalksAtAFifthAndDoesNotSprint()
    {
        armedStage();
        verifyWalkingSpeed(speedOf({}));
        const double sprinting = speedOf({.sprint = true});
        QVERIFY(near(sprinting, QcPhysics::sprintSpeed));
        QVERIFY(player().sprinting);

        send(QcMsg::DrawLauncher{Action::Draw});
        QVERIFY(player().drawing);
        verifyDrawnSpeed(speedOf({.usingItem = true}));
        verifyDrawnSpeed(speedOf({.sprint = true, .usingItem = true}));
        QVERIFY(!player().sprinting);
        QVERIFY(m_client->usingItem);

        const double sneaking = speedOf({.sneak = true, .usingItem = true});
        QVERIFY2(near(sneaking, QcPhysics::sneakSpeed * QcPhysics::usingItemFactor), qPrintable(u"speed %1"_s.arg(sneaking)));
    }

    // The server takes the draw from what it knows: the mark left out gains nothing.
    void aDrawSlowsWithoutTheClientsMark()
    {
        armedStage();
        send(QcMsg::DrawLauncher{Action::Draw});
        verifyDrawnSpeed(speedOf({.sprint = true}));
        QVERIFY(!player().sprinting);
    }

    // A client whose draw the server refused goes on with marked inputs until it hears: they move
    // as predicted, and the state tells the client.
    void theMarkAloneSlowsAndTheStateDeniesTheDraw()
    {
        armedStage();
        QVERIFY(!player().drawing);
        verifyDrawnSpeed(speedOf({.usingItem = true}));
        QVERIFY(!m_client->usingItem);
    }

    void theReleaseAndTheCancelGiveTheSpeedBack()
    {
        armedStage();
        send(QcMsg::DrawLauncher{Action::Draw});
        ticks(10);
        send(QcMsg::DrawLauncher{Action::Release});
        QVERIFY(!player().drawing);
        verifyWalkingSpeed(speedOf({}));
        QVERIFY(!m_client->usingItem);

        send(QcMsg::DrawLauncher{Action::Draw});
        verifyDrawnSpeed(speedOf({}));
        send(QcMsg::DrawLauncher{Action::Cancel});
        verifyWalkingSpeed(speedOf({}));
    }

    // A launcher that left the hand slows nobody, already before the server's tick lets the draw
    // go (QcPlayerLaunchers::tick). The real client goes on marking its inputs until tickLauncher
    // sends the cancel, and those stay slow; this input carries no mark, so only the server's own
    // view of the hand decides.
    void aSwitchedSlotStopsSlowingBeforeTheTick()
    {
        armedStage();
        send(QcMsg::DrawLauncher{Action::Draw});
        const double drawn = speedOf({});
        verifyDrawnSpeed(drawn);

        m_client->connection.send(QcMsg::SelectHotbarSlot{1});
        QcTestUtil::pump();
        QVERIFY(player().drawing);
        QVERIFY(!m_server->systems().projectiles.launchers().draws(player()));

        QcPlayerInput unmarked;
        unmarked.sequence = ++m_sequence;
        unmarked.forward = 1.0f;
        m_client->connection.send(QcMsg::PlayerInput{unmarked});
        QcTestUtil::pump();
        QVERIFY(player().drawing);
        const double speed = std::hypot(player().motion.velocity.x, player().motion.velocity.z);
        QVERIFY2(speed > drawn * 1.3, qPrintable(u"speed %1 after %2"_s.arg(speed).arg(drawn)));

        ticks(1);
        QVERIFY(!player().drawing);
        verifyWalkingSpeed(speedOf({}));
    }

    void aDeathEndsTheDraw()
    {
        armedStage();
        send(QcMsg::DrawLauncher{Action::Draw});
        verifyDrawnSpeed(speedOf({}));

        chat(u"/kill"_s);
        ticks(1);
        QVERIFY(player().dead);
        QVERIFY(!player().drawing);
        send(QcMsg::Respawn{});
        QVERIFY(!player().dead);
        player().inventory.setSlot(0, {item("qtcraft:bow"), 1});
        player().inventory.setSlot(1, {item("qtcraft:arrow"), 16});
        verifyWalkingSpeed(speedOf({}));
        QVERIFY(!m_client->usingItem);
    }

    void noArrowsNoSlowdown()
    {
        armedStage();
        player().inventory.setSlot(1, {});
        send(QcMsg::DrawLauncher{Action::Draw});
        QVERIFY(!player().drawing);
        verifyWalkingSpeed(speedOf({}));
    }

    // The last arrow leaves during the draw: the release shoots nothing and ends the slowdown.
    void arrowsGoneDuringTheDrawEndWithTheRelease()
    {
        armedStage();
        send(QcMsg::DrawLauncher{Action::Draw});
        player().inventory.setSlot(1, {});
        verifyDrawnSpeed(speedOf({}));
        send(QcMsg::DrawLauncher{Action::Release});
        QVERIFY(!player().drawing);
        verifyWalkingSpeed(speedOf({}));
    }
};

QTEST_GUILESS_MAIN(TestDrawSpeed)
#include "test_draw_speed.moc"
