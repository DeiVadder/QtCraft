// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/QcAfterFrame.h"

#include <QQuickWindow>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// Running a world start's step once the window has shown the loading screen (decision E-943):
// after two presented frames, by the fallback when no frame comes (or there is no window), one
// step at a time, and safe when the window goes away. The window is never shown: the test sends
// its frameSwapped signal itself.
class TestAfterFrame : public QObject
{
    Q_OBJECT

private:
    // Lets queued frame signals and zero timers through.
    static void settle() { QCoreApplication::processEvents(); }

private slots:
    void runsAfterTwoFrames()
    {
        QQuickWindow window;
        QcAfterFrame after;
        after.setWindow(&window);
        int runs = 0;
        after.run([&runs] { ++runs; });
        QVERIFY(after.pending());

        emit window.frameSwapped();
        settle();
        QCOMPARE(runs, 0);
        emit window.frameSwapped();
        settle();
        QCOMPARE(runs, 1);
        QVERIFY(!after.pending());

        // Later frames run nothing again.
        emit window.frameSwapped();
        settle();
        QCOMPARE(runs, 1);
    }

    void fallbackWithoutFrames()
    {
        QQuickWindow window;
        QcAfterFrame after;
        after.setWindow(&window);
        int runs = 0;
        after.run([&runs] { ++runs; });
        settle();
        QCOMPARE(runs, 0);
        QTRY_COMPARE_WITH_TIMEOUT(runs, 1, 10 * QcAfterFrame::fallbackMs);
        QVERIFY(!after.pending());
    }

    void withoutWindowRunsAtOnce()
    {
        QcAfterFrame after;
        int runs = 0;
        after.run([&runs] { ++runs; });
        QCOMPARE(runs, 0);
        QTRY_COMPARE(runs, 1);
    }

    void cancelDropsTheStep()
    {
        QQuickWindow window;
        QcAfterFrame after;
        after.setWindow(&window);
        int runs = 0;
        after.run([&runs] { ++runs; });
        after.cancel();
        QVERIFY(!after.pending());

        emit window.frameSwapped();
        emit window.frameSwapped();
        settle();
        QTest::qWait(2 * QcAfterFrame::fallbackMs);
        QCOMPARE(runs, 0);
    }

    void newStepReplacesThePending()
    {
        QQuickWindow window;
        QcAfterFrame after;
        after.setWindow(&window);
        int first = 0;
        int second = 0;
        after.run([&first] { ++first; });
        emit window.frameSwapped();
        settle();
        // The new step waits its own two frames.
        after.run([&second] { ++second; });
        emit window.frameSwapped();
        settle();
        QCOMPARE(second, 0);
        emit window.frameSwapped();
        settle();
        QCOMPARE(first, 0);
        QCOMPARE(second, 1);
    }

    void stepSchedulesTheNext()
    {
        QQuickWindow window;
        QcAfterFrame after;
        after.setWindow(&window);
        QStringList order;
        after.run([&] {
            order.append(u"content"_s);
            after.run([&order] { order.append(u"world"_s); });
        });
        emit window.frameSwapped();
        emit window.frameSwapped();
        settle();
        QCOMPARE(order, QStringList{u"content"_s});
        QVERIFY(after.pending());

        emit window.frameSwapped();
        emit window.frameSwapped();
        settle();
        QCOMPARE(order, (QStringList{u"content"_s, u"world"_s}));
        QVERIFY(!after.pending());
    }

    // A frame queued before the window went (or was replaced) is ignored; the fallback runs the
    // step.
    void windowGoesAway()
    {
        auto window = std::make_unique<QQuickWindow>();
        QcAfterFrame after;
        after.setWindow(window.get());
        int runs = 0;
        after.run([&runs] { ++runs; });
        emit window->frameSwapped();
        emit window->frameSwapped();
        window.reset();
        settle();
        QCOMPARE(runs, 0);
        QTRY_COMPARE_WITH_TIMEOUT(runs, 1, 10 * QcAfterFrame::fallbackMs);

        QQuickWindow first;
        QQuickWindow second;
        after.setWindow(&first);
        after.run([&runs] { ++runs; });
        emit first.frameSwapped();
        emit first.frameSwapped();
        after.setWindow(&second);
        settle();
        QCOMPARE(runs, 1);
        emit second.frameSwapped();
        emit second.frameSwapped();
        settle();
        QCOMPARE(runs, 2);

        after.setWindow(nullptr);
        after.run([&runs] { ++runs; });
        emit second.frameSwapped();
        emit second.frameSwapped();
        settle();
        QTRY_COMPARE(runs, 3);
    }
};

QTEST_MAIN(TestAfterFrame)
#include "test_after_frame.moc"
