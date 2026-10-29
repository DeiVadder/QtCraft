// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcChatFeed.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

// The chat lines of the HUD (decision E-875): they fade after ten seconds while the chat is
// closed, all show again while it is open, and /clearchat empties the view. The clock is the
// test's own; refresh() stands for the feed's timer.
class TestChatFeed : public QObject
{
    Q_OBJECT

private:
    static QStringList texts(const QcChatFeed &feed)
    {
        QStringList list;
        const QList<QcChatFeed::Entry> shown = feed.shown();
        for (const QcChatFeed::Entry &entry : shown) {
            list.append(entry.text);
        }
        return list;
    }

private slots:
    void linesFadeWhileClosed()
    {
        qint64 now = 0;
        QcChatFeed feed;
        feed.setClock([&now] { return now; });
        feed.add(u"first"_s);
        now = 4000;
        feed.add(u"second"_s);
        QCOMPARE(texts(feed), (QStringList{u"first"_s, u"second"_s}));
        QCOMPARE(feed.shown().constFirst().opacity, 1.0);

        // Half way through the first line's fade.
        now = QcChatFeed::fadeAfterMs + QcChatFeed::fadeMs / 2;
        const QList<QcChatFeed::Entry> fading = feed.shown();
        QCOMPARE(fading.constFirst().opacity, 0.5);
        QCOMPARE(fading.constLast().opacity, 1.0);
        // Gone; the second one follows four seconds later.
        now = QcChatFeed::fadeAfterMs + QcChatFeed::fadeMs;
        QCOMPARE(texts(feed), QStringList{u"second"_s});
        now = 4000 + QcChatFeed::fadeAfterMs + QcChatFeed::fadeMs;
        QVERIFY(feed.shown().isEmpty());
        QVERIFY(feed.entries().isEmpty());
    }

    // The feed's timer is due at the next start or end of a fade only, never in between: the HUD
    // animates a fading line itself (fadeMs).
    void theTimerWakesAtFadeStartAndEnd()
    {
        qint64 now = 0;
        QcChatFeed feed;
        feed.setClock([&now] { return now; });
        QCOMPARE(feed.nextRefreshMs(), std::nullopt);
        feed.add(u"first"_s);
        QCOMPARE(feed.nextRefreshMs(), std::optional<qint64>(QcChatFeed::fadeAfterMs));
        QCOMPARE(feed.shown().constFirst().fadeMs, qint64{0});

        // A line arriving later does not move the first wake-up.
        now = 3000;
        feed.add(u"second"_s);
        QCOMPARE(feed.nextRefreshMs(), std::optional<qint64>(QcChatFeed::fadeAfterMs - 3000));

        // The first line starts to fade: it gets its whole fade time, the next wake-up is its end.
        now = QcChatFeed::fadeAfterMs;
        feed.refresh();
        QCOMPARE(feed.shown().constFirst().fadeMs, QcChatFeed::fadeMs);
        QCOMPARE(feed.shown().constLast().fadeMs, qint64{0});
        QCOMPARE(feed.nextRefreshMs(), std::optional<qint64>(QcChatFeed::fadeMs));
        const QVariantMap entry = feed.entries().constFirst().toMap();
        QCOMPARE(entry.value(u"fadeMs"_s).toLongLong(), QcChatFeed::fadeMs);
        QCOMPARE(entry.value(u"opacity"_s).toDouble(), 1.0);

        // Both gone: nothing due any more. Open: nothing fades, nothing due either.
        now = 3000 + QcChatFeed::fadeAfterMs + QcChatFeed::fadeMs;
        feed.refresh();
        QVERIFY(feed.shown().isEmpty());
        QCOMPARE(feed.nextRefreshMs(), std::nullopt);
        feed.add(u"third"_s);
        feed.setOpen(true);
        QCOMPARE(feed.nextRefreshMs(), std::nullopt);
        feed.setOpen(false);
        QVERIFY(feed.nextRefreshMs().has_value());
        feed.clear();
        QCOMPARE(feed.nextRefreshMs(), std::nullopt);
    }

    // The timer itself refreshes the feed when it is due.
    void theTimerRefreshes()
    {
        qint64 now = 0;
        QcChatFeed feed;
        feed.setClock([&now] { return now; });
        feed.add(u"line"_s);
        now = QcChatFeed::fadeAfterMs - 5;
        feed.refresh();
        QCOMPARE(feed.nextRefreshMs(), std::optional<qint64>(5));
        QSignalSpy changed(&feed, &QcChatFeed::entriesChanged);
        now = QcChatFeed::fadeAfterMs;
        QVERIFY(changed.wait(2000));
        QCOMPARE(feed.shown().constFirst().fadeMs, QcChatFeed::fadeMs);
        QCOMPARE(feed.nextRefreshMs(), std::optional<qint64>(QcChatFeed::fadeMs));
    }

    void openShowsTheHistoryAgain()
    {
        qint64 now = 0;
        QcChatFeed feed;
        feed.setClock([&now] { return now; });
        QSignalSpy changed(&feed, &QcChatFeed::entriesChanged);
        for (int i = 0; i < 30; ++i) {
            feed.add(u"line %1"_s.arg(i));
        }
        // Closed: at most the last eight.
        QCOMPARE(feed.shown().size(), QcChatFeed::closedLines);
        QCOMPARE(feed.shown().constLast().text, u"line 29"_s);

        now = 60'000;
        QVERIFY(feed.shown().isEmpty());
        changed.clear();
        feed.setOpen(true);
        QCOMPARE(changed.count(), 1);
        // Open: the last twenty, fully visible, long after they arrived.
        const QList<QcChatFeed::Entry> open = feed.shown();
        QCOMPARE(open.size(), QcChatFeed::openLines);
        QCOMPARE(open.constFirst().text, u"line 10"_s);
        QVERIFY(std::ranges::all_of(open, [](const QcChatFeed::Entry &entry) { return entry.opacity == 1.0; }));
        // The same value again changes nothing.
        feed.setOpen(true);
        QCOMPARE(changed.count(), 1);

        feed.setOpen(false);
        QVERIFY(feed.shown().isEmpty());
    }

    void clearEmptiesTheView()
    {
        QcChatFeed feed;
        feed.add(u"a"_s);
        feed.add(u"b"_s);
        feed.setOpen(true);
        QCOMPARE(feed.shown().size(), 2);
        feed.clear();
        QVERIFY(feed.shown().isEmpty());
        feed.add(u"c"_s);
        QCOMPARE(texts(feed), QStringList{u"c"_s});
        QCOMPARE(QcChatFeed::clearCommand, "/clearchat"_L1);
    }
};

QTEST_GUILESS_MAIN(TestChatFeed)
#include "test_chat_feed.moc"
