#include "clientcore/camera/QcFovEffect.h"
#include "clientcore/player/QcSprintLatch.h"

#include <QTest>

#include <cmath>

// How a sprint feels on the client (decision E-280): the widening of the field of view and when a
// sprint starts and ends.
class TestSprintFeel : public QObject
{
    Q_OBJECT

private slots:
    // 15 % wider while sprinting, eased: most of the way after 0.25 s, back to normal after the
    // sprint, the same at 40 and at 160 frames per second.
    void fieldOfViewWidensSmoothly()
    {
        const auto after = [](double seconds, double frameRate, QcFovEffect::Movement movement, QcFovEffect &effect) {
            const int frames = static_cast<int>(std::lround(seconds * frameRate));
            for (int i = 0; i < frames; ++i) {
                effect.advance(movement, 1.0 / frameRate);
            }
            return effect.factor();
        };
        const QcFovEffect::Movement sprint{.sprinting = true, .flying = false};
        const QcFovEffect::Movement walk{};

        QcFovEffect slow;
        QcFovEffect fast;
        QCOMPARE(slow.factor(), 1.0);
        const double first = after(1.0 / 40.0, 40.0, sprint, slow);
        QVERIFY2(first > 1.0 && first < 1.1, qPrintable(QString::number(first)));
        const double quarterSlow = after(0.25 - 1.0 / 40.0, 40.0, sprint, slow);
        const double quarterFast = after(0.25, 160.0, sprint, fast);
        QVERIFY2(quarterSlow > 1.14 && quarterSlow <= 1.15, qPrintable(QString::number(quarterSlow)));
        QVERIFY(std::abs(quarterSlow - quarterFast) < 1e-3);
        QCOMPARE(after(1.0, 60.0, sprint, slow), 1.15);

        QVERIFY(after(0.05, 60.0, walk, slow) < 1.15);
        QCOMPARE(after(1.0, 60.0, walk, slow), 1.0);
    }

    // The setting scales the widening; at 0 the view stays still. Flying adds its own widening.
    void settingScalesTheWidening()
    {
        QcFovEffect effect;
        effect.setShare(0.5);
        QCOMPARE(effect.target({.sprinting = true, .flying = false}), 1.075);
        effect.setShare(0.0);
        QCOMPARE(effect.target({.sprinting = true, .flying = true}), 1.0);
        QCOMPARE(effect.advance({.sprinting = true, .flying = false}, 1.0), 1.0);
        effect.setShare(1.0);
        QCOMPARE(effect.target({.sprinting = false, .flying = true}), 1.1);
        QCOMPARE(effect.target({.sprinting = true, .flying = true}), 1.25);
        effect.setShare(7.0);
        QCOMPARE(effect.share(), 1.0);
        effect.setShare(std::nan(""));
        QCOMPARE(effect.share(), 1.0);
        // A broken frame time leaves the factor alone.
        QCOMPARE(effect.advance({.sprinting = true, .flying = false}, std::nan("")), 1.0);
    }

    // A press of the sprint key starts a sprint that lasts while forward stays held.
    void sprintKeyLatchesWhileForwardIsHeld()
    {
        QcSprintLatch latch;
        QVERIFY(!latch.update(true, false, false));
        QVERIFY(latch.update(true, true, false));
        QVERIFY(latch.update(true, false, false));
        QVERIFY(latch.update(true, false, false));
        // Releasing forward ends it; pressing forward again walks.
        QVERIFY(!latch.update(false, false, false));
        QVERIFY(!latch.update(true, false, false));
        // Sneaking ends it as well; the sprint key alone does not sprint without forward.
        QVERIFY(latch.update(true, true, false));
        QVERIFY(!latch.update(true, false, true));
        QVERIFY(!latch.update(true, false, false));
        QVERIFY(!latch.update(false, true, false));
    }

    // Two presses of forward within 300 ms start a sprint; slower presses walk.
    void doubleTapOnForwardSprints()
    {
        QcSprintLatch latch;
        latch.forwardPressed(1000);
        QVERIFY(!latch.update(true, false, false));
        QVERIFY(!latch.update(false, false, false));
        latch.forwardPressed(1250);
        QVERIFY(latch.update(true, false, false));
        QVERIFY(latch.update(true, false, false));
        QVERIFY(!latch.update(false, false, false));

        latch.forwardPressed(2000);
        QVERIFY(!latch.update(false, false, false));
        latch.forwardPressed(2400);
        QVERIFY(!latch.update(true, false, false));
        // A clock going backwards is no double tap.
        latch.forwardPressed(2300);
        QVERIFY(!latch.update(true, false, false));
        QVERIFY(!latch.update(false, false, false));

        // A short first tap that no frame saw still counts.
        latch.forwardPressed(3000);
        latch.forwardPressed(3150);
        QVERIFY(latch.update(true, false, false));
    }

    // A screen, death or focus loss forgets the sprint and the first tap of a double tap.
    void resetForgetsSprintAndTaps()
    {
        QcSprintLatch latch;
        QVERIFY(latch.update(true, true, false));
        latch.reset();
        QVERIFY(!latch.update(true, false, false));

        latch.forwardPressed(1000);
        latch.reset();
        latch.forwardPressed(1100);
        QVERIFY(!latch.update(true, false, false));
    }
};

QTEST_GUILESS_MAIN(TestSprintFeel)
#include "test_sprint_feel.moc"
