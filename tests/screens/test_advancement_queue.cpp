// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcAdvancementQueue.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

// The toasts of reached advancements (decisions E-923, E-954): one shows at once, at most maxWaiting
// wait behind it, the rest come as one "+N more" toast, and clear() forgets all of it.
class TestAdvancementQueue : public QObject
{
    Q_OBJECT

private:
    static QVariantMap advancement(int number)
    {
        return {{u"name"_s, u"t:a%1"_s.arg(number)}, {u"title"_s, u"A%1"_s.arg(number)}, {u"special"_s, false}};
    }

    static QString shownName(const QcAdvancementQueue &queue) { return queue.current().value(u"name"_s).toString(); }

private slots:
    void firstShowsAtOnce()
    {
        QcAdvancementQueue queue;
        QSignalSpy changed(&queue, &QcAdvancementQueue::currentChanged);
        QVERIFY(!queue.showing());
        queue.add(advancement(1));
        QVERIFY(queue.showing());
        QCOMPARE(shownName(queue), u"t:a1"_s);
        QCOMPARE(queue.waiting().size(), qsizetype{0});
        QCOMPARE(changed.count(), 1);

        // A second one waits until the first has gone.
        queue.add(advancement(2));
        QCOMPARE(shownName(queue), u"t:a1"_s);
        QCOMPARE(queue.waiting().size(), qsizetype{1});
        QCOMPARE(changed.count(), 1);
        queue.next();
        QCOMPARE(shownName(queue), u"t:a2"_s);
        queue.next();
        QVERIFY(!queue.showing());
        QCOMPARE(changed.count(), 3);

        // Nothing on show and nothing waiting: next() changes nothing.
        queue.next();
        QCOMPARE(changed.count(), 3);
    }

    // Two equal toasts in a row each notify, so each slides in.
    void equalToastsNotifyEach()
    {
        QcAdvancementQueue queue;
        QSignalSpy changed(&queue, &QcAdvancementQueue::currentChanged);
        queue.add(advancement(1));
        queue.add(advancement(1));
        queue.next();
        QCOMPARE(changed.count(), 2);
        QCOMPARE(shownName(queue), u"t:a1"_s);
    }

    void beyondTheQueueFoldsIntoOne()
    {
        QcAdvancementQueue queue;
        const int beyond = 3;
        // One on show, maxWaiting waiting, `beyond` folded.
        for (int number = 0; number < 1 + QcAdvancementQueue::maxWaiting + beyond; ++number) {
            queue.add(advancement(number));
        }
        QCOMPARE(queue.waiting().size(), qsizetype{QcAdvancementQueue::maxWaiting});
        QCOMPARE(queue.folded(), beyond);

        for (int number = 1; number <= QcAdvancementQueue::maxWaiting; ++number) {
            queue.next();
            QCOMPARE(shownName(queue), u"t:a%1"_s.arg(number));
        }
        queue.next();
        QCOMPARE(shownName(queue), u"more"_s);
        QCOMPARE(queue.current().value(u"title"_s).toString(), u"+3 more"_s);
        QCOMPARE(queue.current().value(u"special"_s).toBool(), false);
        QCOMPARE(queue.folded(), 0);
        queue.next();
        QVERIFY(!queue.showing());
    }

    void clearForgetsEverything()
    {
        QcAdvancementQueue queue;
        for (int number = 0; number < 10; ++number) {
            queue.add(advancement(number));
        }
        QSignalSpy changed(&queue, &QcAdvancementQueue::currentChanged);
        queue.clear();
        QVERIFY(!queue.showing());
        QCOMPARE(queue.waiting().size(), qsizetype{0});
        QCOMPARE(queue.folded(), 0);
        QCOMPARE(changed.count(), 1);
        queue.clear();
        QCOMPARE(changed.count(), 1);

        queue.add(advancement(11));
        QCOMPARE(shownName(queue), u"t:a11"_s);
    }
};

QTEST_MAIN(TestAdvancementQueue)
#include "test_advancement_queue.moc"
