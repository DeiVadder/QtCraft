// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/settings/QcDeviceLossPolicy.h"

#include <QTest>

using Response = QcDeviceLossPolicy::Response;

namespace {

constexpr qint64 coolDown = QcDeviceLossPolicy::defaultCoolDownMs;
constexpr qint64 window = QcDeviceLossPolicy::repeatWindowMs;

} // namespace

// What the game does after a lost graphics device (decision E-963), on a fake clock: a cool-down
// of classic frames after the first loss in the realistic mode, the classic look for the session
// after a second one within the repeat window, nothing for losses in the classic look. The
// sequences are the ones the game can produce: during a cool-down it draws the classic look, so a
// loss then is a classic one (QcGraphicsGuard passes what the view drew).
class TestDeviceLossPolicy : public QObject
{
    Q_OBJECT

private slots:
    void realisticIsAllowedWithoutLosses()
    {
        const QcDeviceLossPolicy policy;
        QVERIFY(policy.realisticAllowed(0));
        QVERIFY(!policy.fellBack());
        QCOMPARE(policy.coolDownLeftMs(0), qint64{0});
        QCOMPARE(policy.coolDownMs(), coolDown);
    }

    void aLossInTheClassicLookChangesNothing()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(1000, false), Response::Recover);
        QCOMPARE(policy.deviceLost(2000, false), Response::Recover);
        QVERIFY(policy.realisticAllowed(2000));
        // The classic losses are no first realistic loss: the next realistic one only cools down.
        QCOMPARE(policy.deviceLost(3000, true), Response::CoolDown);
    }

    void theFirstRealisticLossCoolsDown()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(10'000, true), Response::CoolDown);
        QVERIFY(!policy.realisticAllowed(10'000));
        QCOMPARE(policy.coolDownLeftMs(10'000), coolDown);
        QVERIFY(!policy.realisticAllowed(10'000 + coolDown - 1));
        QCOMPARE(policy.coolDownLeftMs(10'000 + coolDown - 1), qint64{1});
        QVERIFY(policy.realisticAllowed(10'000 + coolDown));
        QCOMPARE(policy.coolDownLeftMs(10'000 + coolDown), qint64{0});
        QVERIFY(!policy.fellBack());
    }

    void aShortCoolDownForTests()
    {
        QcDeviceLossPolicy policy(50);
        QCOMPARE(policy.deviceLost(0, true), Response::CoolDown);
        QCOMPARE(policy.coolDownLeftMs(0), qint64{50});
        QVERIFY(policy.realisticAllowed(50));
        // Choosing again keeps the length.
        policy.choseAgain();
        QCOMPARE(policy.coolDownMs(), qint64{50});
    }

    // The playtest's loop (2026-11-02): a loss about every 16 s while the menu stayed open; with
    // the cool-down the second realistic loss comes after it.
    void aSecondRealisticLossWithinTheWindowFallsBack()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(0, true), Response::CoolDown);
        const qint64 second = coolDown + 5000;
        QCOMPARE(policy.deviceLost(second, true), Response::FallBack);
        QVERIFY(policy.fellBack());
        QVERIFY(!policy.realisticAllowed(second + 10 * window));
        QCOMPARE(policy.coolDownLeftMs(second), qint64{0});
        // Further losses draw the classic look anyway.
        QCOMPARE(policy.deviceLost(second + 1000, true), Response::Recover);
        QCOMPARE(policy.deviceLost(second + 2000, false), Response::Recover);
    }

    void theWindowEndsInclusively()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(0, true), Response::CoolDown);
        QCOMPARE(policy.deviceLost(window, true), Response::FallBack);
    }

    void lossesFarApartOnlyCoolDown()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(0, true), Response::CoolDown);
        const qint64 later = window + 1;
        QCOMPARE(policy.deviceLost(later, true), Response::CoolDown);
        // Measured from the latest loss.
        QCOMPARE(policy.deviceLost(later + window, true), Response::FallBack);
    }

    // The game draws the classic look during a cool-down: a loss then is classic and changes
    // neither the time left nor the window of the realistic loss before.
    void aClassicLossDuringTheCoolDownChangesNothing()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(0, true), Response::CoolDown);
        QCOMPARE(policy.deviceLost(5000, false), Response::Recover);
        QCOMPARE(policy.coolDownLeftMs(5000), coolDown - 5000);
        QVERIFY(policy.realisticAllowed(coolDown));
        // Still within the window of the loss at 0, not of the classic one at 5000.
        QCOMPARE(policy.deviceLost(window, true), Response::FallBack);
    }

    // Not reachable in the game (it draws classic during a cool-down), defined anyway: it counts
    // like any realistic loss and falls back within the window.
    void aRealisticLossDuringTheCoolDownFallsBack()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(0, true), Response::CoolDown);
        QCOMPARE(policy.deviceLost(1000, true), Response::FallBack);
        QVERIFY(policy.fellBack());
    }

    void choosingAgainForgetsTheLosses()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(0, true), Response::CoolDown);
        const qint64 second = coolDown + 1000;
        QCOMPARE(policy.deviceLost(second, true), Response::FallBack);
        policy.choseAgain();
        QVERIFY(!policy.fellBack());
        QVERIFY(policy.realisticAllowed(second));
        // The losses before count no more: the next one within the old window only cools down.
        QCOMPARE(policy.deviceLost(second + 1000, true), Response::CoolDown);
    }

    void choosingAgainEndsACoolDownAtOnce()
    {
        QcDeviceLossPolicy policy;
        QCOMPARE(policy.deviceLost(0, true), Response::CoolDown);
        QVERIFY(!policy.realisticAllowed(1000));
        policy.choseAgain();
        QVERIFY(policy.realisticAllowed(1000));
        QCOMPARE(policy.coolDownLeftMs(1000), qint64{0});
        QCOMPARE(policy.deviceLost(2000, true), Response::CoolDown);
    }
};

QTEST_GUILESS_MAIN(TestDeviceLossPolicy)
#include "test_device_loss_policy.moc"
