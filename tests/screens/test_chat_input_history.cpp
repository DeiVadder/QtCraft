// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcChatFeed.h"
#include "app/controllers/QcChatInputHistory.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

using namespace Qt::StringLiterals;

// The sent lines of the chat line under arrow up and down (decision E-970): back through them,
// forward again, the draft past the newest one, no empty lines, no line twice in a row, at most
// QcChatInputHistory::limit lines.
class TestChatInputHistory : public QObject
{
    Q_OBJECT

private:
    static void send(QcChatInputHistory &history, const QStringList &lines)
    {
        for (const QString &line : lines) {
            history.remember(line);
        }
    }

private slots:
    void stepsBackThroughTheSentLines()
    {
        QcChatInputHistory history;
        send(history, {u"hello"_s, u"/time set day"_s, u"bye"_s});

        QCOMPARE(history.previous(u""_s), u"bye"_s);
        QCOMPARE(history.previous(u"bye"_s), u"/time set day"_s);
        QCOMPARE(history.previous(u"/time set day"_s), u"hello"_s);
        // The oldest line stays.
        QCOMPARE(history.previous(u"hello"_s), u"hello"_s);
    }

    void stepsForwardAndBringsTheDraftBack()
    {
        QcChatInputHistory history;
        send(history, {u"first"_s, u"second"_s});

        QCOMPARE(history.previous(u"half typ"_s), u"second"_s);
        QCOMPARE(history.previous(u"second"_s), u"first"_s);
        QCOMPARE(history.next(u"first"_s), u"second"_s);
        QCOMPARE(history.next(u"second"_s), u"half typ"_s);
        // Past the draft nothing changes.
        QCOMPARE(history.next(u"half typ"_s), u"half typ"_s);
        // Back again from the draft: the newest line, and the draft is kept anew.
        QCOMPARE(history.previous(u"half typed"_s), u"second"_s);
        QCOMPARE(history.next(u"second"_s), u"half typed"_s);
    }

    void aChangedRecalledLineComesBackAsTheDraft()
    {
        QcChatInputHistory history;
        send(history, {u"first"_s, u"second"_s});

        QCOMPARE(history.previous(u"typed"_s), u"second"_s);
        // Changed and stepped on: the change is the draft, the stored line stays as sent.
        QCOMPARE(history.previous(u"second!"_s), u"first"_s);
        QCOMPARE(history.next(u"first"_s), u"second"_s);
        QCOMPARE(history.next(u"second"_s), u"second!"_s);
        QCOMPARE(history.lines(), (QList<QString>{u"first"_s, u"second"_s}));

        // Changed and stepped forward past the newest line: the change comes back at once.
        QCOMPARE(history.previous(u"second!"_s), u"second"_s);
        QCOMPARE(history.next(u"second?"_s), u"second?"_s);
        // At the oldest line a change stays on the line and becomes the draft on the next step.
        QCOMPARE(history.previous(u"second?"_s), u"second"_s);
        QCOMPARE(history.previous(u"second"_s), u"first"_s);
        QCOMPARE(history.previous(u"first?"_s), u"first?"_s);
        QCOMPARE(history.next(u"first?"_s), u"second"_s);
        QCOMPARE(history.next(u"second"_s), u"first?"_s);
    }

    void withoutLinesTheTextStays()
    {
        QcChatInputHistory history;
        QCOMPARE(history.previous(u"draft"_s), u"draft"_s);
        QCOMPARE(history.next(u"draft"_s), u"draft"_s);
    }

    void emptyLinesAreNotStored()
    {
        QcChatInputHistory history;
        send(history, {u""_s, u"   "_s, u"hi"_s, u""_s});
        QCOMPARE(history.lines(), QList<QString>{u"hi"_s});
    }

    void theSameLineTwiceInARowIsStoredOnce()
    {
        QcChatInputHistory history;
        send(history, {u"/home"_s, u"/home"_s, u" /home "_s, u"hi"_s, u"/home"_s});
        QCOMPARE(history.lines(), (QList<QString>{u"/home"_s, u"hi"_s, u"/home"_s}));
    }

    void anEditedRecalledLineIsANewEntry()
    {
        QcChatInputHistory history;
        send(history, {u"/give @s stone 1"_s, u"hi"_s});
        QCOMPARE(history.previous(u""_s), u"hi"_s);
        QCOMPARE(history.previous(u"hi"_s), u"/give @s stone 1"_s);
        history.remember(u"/give @s stone 64"_s);

        QCOMPARE(history.lines(), (QList<QString>{u"/give @s stone 1"_s, u"hi"_s, u"/give @s stone 64"_s}));
        // Sending starts the steps from the newest line again, with an empty draft.
        QCOMPARE(history.next(u""_s), u""_s);
        QCOMPARE(history.previous(u""_s), u"/give @s stone 64"_s);
        QCOMPARE(history.next(u"/give @s stone 64"_s), u""_s);
    }

    void keepsAtMostTheLimit()
    {
        QcChatInputHistory history;
        for (qsizetype i = 0; i < QcChatInputHistory::limit + 5; ++i) {
            history.remember(QString::number(i));
        }
        QCOMPARE(history.lines().size(), QcChatInputHistory::limit);
        QCOMPARE(history.lines().constFirst(), u"5"_s);

        QString shown;
        for (qsizetype i = 0; i < QcChatInputHistory::limit + 2; ++i) {
            shown = history.previous(shown);
        }
        QCOMPARE(shown, u"5"_s);
    }

    void openingTheChatStartsFromTheNewestLine()
    {
        QcChatFeed feed;
        QcChatInputHistory &history = *feed.inputs();
        send(history, {u"first"_s, u"second"_s});
        feed.setOpen(true);
        QCOMPARE(history.previous(u"draft"_s), u"second"_s);
        QCOMPARE(history.previous(u"second"_s), u"first"_s);

        // Closed without sending (Esc) and opened again: the newest line first, the old draft gone.
        feed.setOpen(false);
        feed.setOpen(true);
        QCOMPARE(history.next(u""_s), u""_s);
        QCOMPARE(history.previous(u""_s), u"second"_s);
        QCOMPARE(history.next(u"second"_s), u""_s);
    }
};

QTEST_GUILESS_MAIN(TestChatInputHistory)
#include "test_chat_input_history.moc"
