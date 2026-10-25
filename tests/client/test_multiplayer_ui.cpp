#include "app/multiplayer/QcLanAddresses.h"
#include "app/multiplayer/QcMultiplayerSettings.h"
#include "app/multiplayer/QcPlayerList.h"
#include "app/multiplayer/QcServerHistory.h"
#include "common/diagnostics/QcRoundTripStats.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The parts of the multiplayer screens without a window (decisions E-258 to E-260): the list of recent
// servers, the addresses shown while hosting, the join and leave lines of the player list and what
// the settings file keeps.
class TestMultiplayerUi : public QObject
{
    Q_OBJECT

private:
    static QDateTime at(int minute) { return QDateTime(QDate(2026, 10, 11), QTime(10, minute), QTimeZone::UTC); }

    static QStringList addresses(const QcServerHistory &history)
    {
        QStringList list;
        for (const QcServerHistory::Entry &entry : history.entries()) {
            list.append(entry.address);
        }
        return list;
    }

private slots:
    void historyPutsTheLatestServerFirstAndMergesSameAddresses()
    {
        QcServerHistory history;
        history.add(u"alpha.example"_s, u"Alpha"_s, at(1));
        history.add(u"192.168.1.20:25601"_s, u"Bravo"_s, at(2));
        // The default port written out and another case are the same server: one entry, moved up,
        // with the new name and time.
        history.add(u"ALPHA.example:25600"_s, u"Alpha 2"_s, at(3));

        QCOMPARE(addresses(history), (QStringList{u"ALPHA.example:25600"_s, u"192.168.1.20:25601"_s}));
        QCOMPARE(history.entries().first().name, u"Alpha 2"_s);
        QCOMPARE(history.entries().first().lastJoined, at(3));
        // Another port is another server.
        history.add(u"192.168.1.20"_s, {}, at(4));
        QCOMPARE(history.entries().size(), 3);
    }

    void historyIgnoresBadAddressesAndKeepsTheNewestTen()
    {
        QcServerHistory history;
        history.add(u"host:notaport"_s, u"Broken"_s, at(0));
        history.add(u"   "_s, {}, at(0));
        QVERIFY(history.entries().isEmpty());

        for (int i = 0; i < 12; ++i) {
            history.add(u"10.0.0.%1"_s.arg(i), {}, at(i));
        }
        QCOMPARE(history.entries().size(), QcServerHistory::maxEntries);
        QCOMPARE(history.entries().first().address, u"10.0.0.11"_s);
        QCOMPARE(history.entries().last().address, u"10.0.0.2"_s);
    }

    void historyRemovesByHostAndPort()
    {
        QcServerHistory history;
        history.add(u"[::1]:25600"_s, {}, at(0));
        history.add(u"localhost"_s, {}, at(1));
        QVERIFY(history.remove(u"::1"_s));
        QVERIFY(!history.remove(u"::1"_s));
        QVERIFY(!history.remove(u"localhost:1"_s));
        QCOMPARE(addresses(history), QStringList{u"localhost"_s});
    }

    void historyRoundTripsThroughTheSettingsFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QcServerHistory history;
        history.add(u"b.example:1234"_s, u"B"_s, at(1));
        history.add(u"a.example"_s, u"A, with comma"_s, at(2));
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(u"viewDistance"_s, 12);
            history.save(file);
        }

        QSettings file(path, QSettings::IniFormat);
        const QcServerHistory loaded = QcServerHistory::load(file);
        QCOMPARE(loaded.entries(), history.entries());
        // Other settings in the file stay.
        QCOMPARE(file.value(u"viewDistance"_s).toInt(), 12);
    }

    void historySkipsDamagedEntriesInTheFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        QFile raw(path);
        QVERIFY(raw.open(QIODevice::WriteOnly));
        raw.write("[multiplayer]\nservers\\size=4\nservers\\1\\address=good.example\nservers\\2\\address=\n"
                  "servers\\3\\address=bad:port:here:x\nservers\\4\\address=GOOD.example:25600\n");
        raw.close();

        QSettings file(path, QSettings::IniFormat);
        const QcServerHistory loaded = QcServerHistory::load(file);
        // The first entry is the newest; its duplicate further down is dropped.
        QCOMPARE(addresses(loaded), QStringList{u"good.example"_s});
    }

    void settingsKeepNameServersAndHostChoicesButNoPassword()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"settings.ini"_s);
        {
            QcMultiplayerSettings settings(path);
            QCOMPARE(settings.hostPort(), 25600);
            QCOMPARE(settings.hostMaxPlayers(), 8);
            QSignalSpy changed(&settings, &QcMultiplayerSettings::changed);
            settings.setPlayerName(u"  Alex\u0007  "_s);
            QCOMPARE(settings.playerName(), u"Alex"_s);
            settings.rememberServer(u"island.example"_s, u"Island"_s);
            settings.rememberHost(25700, 500);
            QCOMPARE(changed.count(), 3);
            QCOMPARE(settings.hostMaxPlayers(), QcMultiplayerSettings::maxHostPlayers);
        }

        const QcMultiplayerSettings loaded(path);
        QCOMPARE(loaded.playerName(), u"Alex"_s);
        QCOMPARE(loaded.hostPort(), 25700);
        const QVariantList servers = loaded.recentServers();
        QCOMPARE(servers.size(), 1);
        const QVariantMap row = servers.first().toMap();
        QCOMPARE(row.value(u"address"_s).toString(), u"island.example"_s);
        QCOMPARE(row.value(u"name"_s).toString(), u"Island"_s);
        QVERIFY(!row.value(u"lastJoined"_s).toString().isEmpty());

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().toLower().contains("password"));
    }

    void settingsNamesAreCutLikeOnTheServer()
    {
        QTemporaryDir dir;
        QcMultiplayerSettings settings(dir.filePath(u"settings.ini"_s));
        settings.setPlayerName(QString(40, u'x'));
        QCOMPARE(settings.playerName().size(), QcMultiplayerSettings::maxNameLength);
        QCOMPARE(settings.cleanName(u"\u0001\u0002"_s), QString());
    }

    void lanAddressesLeaveOutWhatOthersCannotReach()
    {
        QHostAddress scoped(u"fe80::1"_s);
        scoped.setScopeId(u"en0"_s);
        const QList<QHostAddress> addresses{QHostAddress(u"2001:db8::5"_s), QHostAddress::LocalHost, QHostAddress(u"192.168.1.20"_s),
                                            QHostAddress::LocalHostIPv6, scoped, QHostAddress(u"169.254.3.4"_s),
                                            QHostAddress(u"10.0.0.2"_s), QHostAddress(u"192.168.1.20"_s), QHostAddress::AnyIPv4};
        QCOMPARE(QcLanAddresses::describe(addresses, 25600),
                 (QStringList{u"10.0.0.2:25600"_s, u"192.168.1.20:25600"_s, u"[2001:db8::5]:25600"_s}));
        // A listener bound to this computer only: loopback is what reaches it.
        QCOMPARE(QcLanAddresses::describe({QHostAddress::LocalHost}, 25611, true), QStringList{u"127.0.0.1:25611"_s});
    }

    void lanAddressesAreLimited()
    {
        QList<QHostAddress> many;
        for (int i = 1; i <= 9; ++i) {
            many.append(QHostAddress(u"10.0.0.%1"_s.arg(i)));
        }
        QCOMPARE(QcLanAddresses::describe(many, 1).size(), QcLanAddresses::maxAddresses);
    }

    void playerListNamesWhoCameAndWent()
    {
        using Entry = QcPlayerList::Entry;
        const QList<Entry> before{{1, u"Me"_s, true}, {2, u"Alex"_s, false}, {3, u"Sam"_s, false}};
        // Sam left, Kim came, Alex became an operator (no line), the own entry is ignored even when
        // its id changes in a broken list.
        const QList<Entry> after{{2, u"Alex"_s, true}, {4, u"Kim"_s, false}, {1, u"Me"_s, true}};
        const QcPlayerList::Changes changes = QcPlayerList::compare(before, after, 1);
        QCOMPARE(changes.joined, QStringList{u"Kim"_s});
        QCOMPARE(changes.left, QStringList{u"Sam"_s});

        const QcPlayerList::Changes same = QcPlayerList::compare(after, after, 1);
        QVERIFY(same.joined.isEmpty() && same.left.isEmpty());
        // A player who rejoined has a new entity: he left and came.
        const QcPlayerList::Changes rejoined = QcPlayerList::compare({{5, u"Bo"_s, false}}, {{6, u"Bo"_s, false}}, 1);
        QCOMPARE(rejoined.joined, QStringList{u"Bo"_s});
        QCOMPARE(rejoined.left, QStringList{u"Bo"_s});
    }

    void playerListShowsRoundTripsAsBars()
    {
        // As in the reference (decision E-886): five bars below 150 ms down to one from a second on.
        QCOMPARE(QcListedLatency::bars(-1), 0);
        QCOMPARE(QcListedLatency::bars(0), 5);
        QCOMPARE(QcListedLatency::bars(149'999), 5);
        QCOMPARE(QcListedLatency::bars(150'000), 4);
        QCOMPARE(QcListedLatency::bars(299'999), 4);
        QCOMPARE(QcListedLatency::bars(300'000), 3);
        QCOMPARE(QcListedLatency::bars(600'000), 2);
        QCOMPARE(QcListedLatency::bars(999'999), 2);
        QCOMPARE(QcListedLatency::bars(1'000'000), 1);
        QCOMPARE(QcListedLatency::bars(60'000'000), 1);
    }

    void playerListWithoutSessionIsEmpty()
    {
        QcPlayerList list;
        QSignalSpy changed(&list, &QcPlayerList::changed);
        list.follow(nullptr);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(list.count(), 0);
        QVERIFY(list.players().isEmpty());
        QVERIFY(list.serverName().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMultiplayerUi)
#include "test_multiplayer_ui.moc"
