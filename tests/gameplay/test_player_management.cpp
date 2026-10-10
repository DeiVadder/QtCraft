#include "QcAccessList.h"
#include "QcProfileValidation.h"
#include "QcServerTestFixture.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// Kick, ban, whitelist and operators (decision E-244), their file access.json and the checks when a
// player joins.
class TestPlayerManagement : public QcServerTestFixture
{
    Q_OBJECT

private:
    std::unique_ptr<RawClient> connect(const QString &name, QcServer::ClientOrigin origin = QcServer::ClientOrigin::Remote)
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), origin);
        auto client = std::make_unique<RawClient>(std::move(clientEnd));
        client->connection.send(QcMsg::Hello{QcProtocol::version, name, {}});
        pump();
        ticks(2);
        return client;
    }

    QString lastLine() const { return m_client->chatLines.isEmpty() ? QString() : m_client->chatLines.constLast(); }

    QcServerConfig savedWorld(const QTemporaryDir &directory) const
    {
        QcServerConfig config;
        config.worldDirectory = directory.path();
        return config;
    }

    QJsonObject accessFile(const QTemporaryDir &directory) const
    {
        QFile file(directory.filePath(u"access.json"_s));
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void kickDisconnectsAPlayerWhoMayReturn()
    {
        join();
        std::unique_ptr<RawClient> other = connect(u"Griefer"_s);
        QCOMPARE(m_server->clientCount(), 2);

        chat(u"/kick griefer spamming the chat"_s);
        QCOMPARE(lastLine(), u"Kicked griefer"_s);
        ticks(1);
        QCOMPARE(other->disconnectReason, u"Kicked by Raw: spamming the chat"_s);
        QCOMPARE(m_server->clientCount(), 1);
        chat(u"/kick Griefer"_s);
        QCOMPARE(lastLine(), u"No player named Griefer is online"_s);

        // Kicked is not banned.
        other = connect(u"Griefer"_s);
        QVERIFY(other->disconnectReason.isEmpty());
        QCOMPARE(m_server->clientCount(), 2);
    }

    void bansAreKeptAndCheckedOnJoin()
    {
        QTemporaryDir directory;
        join(savedWorld(directory));
        std::unique_ptr<RawClient> other = connect(u"Griefer"_s);
        chat(u"/ban Griefer broke the spawn"_s);
        QCOMPARE(lastLine(), u"Banned Griefer"_s);
        ticks(1);
        QCOMPARE(other->disconnectReason, u"Banned by Raw: broke the spawn"_s);
        QCOMPARE(accessFile(directory).value(u"bans"_s).toArray().first().toObject().value(u"reason"_s).toString(), u"broke the spawn"_s);

        // After a restart the ban holds, in any case; the banlist names it.
        other.reset();
        cleanup();
        join(savedWorld(directory));
        other = connect(u"GRIEFER"_s);
        QCOMPARE(other->disconnectReason, u"You are banned from this world: broke the spawn"_s);
        QCOMPARE(m_server->clientCount(), 1);
        chat(u"/banlist"_s);
        QVERIFY(lastLine().startsWith(u"Griefer (Raw, "_s));

        chat(u"/pardon griefer"_s);
        QCOMPARE(lastLine(), u"Unbanned griefer"_s);
        chat(u"/unban griefer"_s);
        QCOMPARE(lastLine(), u"griefer is not banned"_s);
        other = connect(u"Griefer"_s);
        QVERIFY(other->disconnectReason.isEmpty());
    }

    void theWhitelistAdmitsListedPlayersAndOperators()
    {
        QTemporaryDir directory;
        join(savedWorld(directory));
        chat(u"/whitelist on"_s);
        chat(u"/whitelist add Friend"_s);
        QCOMPARE(lastLine(), u"Friend is on the whitelist"_s);

        QCOMPARE(connect(u"Stranger"_s)->disconnectReason, u"You are not on the whitelist of this world"_s);
        QVERIFY(connect(u"friend"_s)->disconnectReason.isEmpty());
        // Operators and the local player always come in.
        chat(u"/op Helper"_s);
        QVERIFY(connect(u"Helper"_s)->disconnectReason.isEmpty());
        QVERIFY(connect(u"Stranger"_s, QcServer::ClientOrigin::Local)->disconnectReason.isEmpty());

        chat(u"/whitelist list"_s);
        QCOMPARE(lastLine(), u"The whitelist is on: Friend"_s);
        const QJsonObject whitelist = accessFile(directory).value(u"whitelist"_s).toObject();
        QCOMPARE(whitelist.value(u"enabled"_s).toBool(), true);
        QCOMPARE(whitelist.value(u"names"_s).toArray(), QJsonArray{u"Friend"_s});

        chat(u"/whitelist remove friend"_s);
        chat(u"/whitelist off"_s);
        QVERIFY(connect(u"Stranger"_s)->disconnectReason.isEmpty());
        chat(u"/whitelist maybe"_s);
        QVERIFY(lastLine().startsWith(u"Usage: /whitelist"_s));
    }

    void operatorsAreKeptWithTheWorld()
    {
        QTemporaryDir directory;
        join(savedWorld(directory));
        chat(u"/op Helper"_s);
        QCOMPARE(lastLine(), u"Helper is an operator"_s);
        chat(u"/op"_s);
        QVERIFY(lastLine().contains(u"Helper"_s));
        cleanup();

        // Helper is an operator after the restart, Raw (only in the test's configuration) is not.
        join(notOperator(savedWorld(directory)));
        std::unique_ptr<RawClient> helper = connect(u"Helper"_s);
        helper->connection.send(QcMsg::ChatInput{u"/give dirt 2"_s});
        pump();
        QCOMPARE(helper->chatLines.constLast(), u"Gave 2 x qtcraft:dirt"_s);
        chat(u"/give dirt"_s);
        QCOMPARE(lastLine(), u"Only operators can use /give"_s);
    }

    void aDamagedAccessFileClosesTheWorldToStrangers()
    {
        QTemporaryDir directory;
        const QByteArray damaged = "{\"format\": 1, \"operators\": [\"Al";
        {
            QFile file(directory.filePath(u"access.json"_s));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(damaged);
        }
        join(notOperator(savedWorld(directory)));
        QCOMPARE(connect(u"Stranger"_s)->disconnectReason, u"You are not on the whitelist of this world"_s);
        // Strangers do not even get in; a player without rights who did would not be told.
        QVERIFY(std::ranges::none_of(m_client->chatLines, [](const QString &line) { return line.contains(u"access.json"_s); }));

        // The host (an operator) hears of it, translatable, with the place of the copy.
        std::unique_ptr<RawClient> host = connect(u"Host"_s, QcServer::ClientOrigin::Local);
        QVERIFY(host->disconnectReason.isEmpty());
        const QString copy = directory.filePath(u"access.json.damaged"_s);
        const auto warning = std::ranges::find_if(host->chatKeys, [](const QcMsg::ChatMessage &line) {
            return line.key.startsWith(u"access.json was damaged"_s);
        });
        QVERIFY(warning != host->chatKeys.end());
        QCOMPARE(warning->arguments, QStringList{copy});

        // The first change writes new lists; the copy keeps the damaged ones.
        host->connection.send(QcMsg::ChatInput{u"/op Helper"_s});
        pump();
        QCOMPARE(host->chatLines.constLast(), u"Helper is an operator"_s);
        QCOMPARE(accessFile(directory).value(u"operators"_s).toArray(), QJsonArray{u"Helper"_s});
        QFile kept(copy);
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), damaged);
    }

    void aDamagedFileThatCannotBeCopiedIsNotOverwritten()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(u"access.json"_s);
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{\"format\": 1, \"bans\": [");
        }
        // Unreadable: neither read nor copied.
        QVERIFY(QFile::setPermissions(path, QFileDevice::WriteOwner));
        join(savedWorld(directory));
        const auto restore = qScopeGuard([&path] { QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner); });
        QVERIFY(!QFile::exists(path + u".damaged"_s));
        QVERIFY(std::ranges::any_of(m_client->chatKeys, [](const QcMsg::ChatMessage &line) {
            return line.key.startsWith(u"access.json is damaged and could not be copied aside"_s);
        }));

        chat(u"/whitelist add Friend"_s);
        QVERIFY2(lastLine().startsWith(u"The change applies but could not be saved: "_s), qPrintable(lastLine()));
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{\"format\": 1, \"bans\": ["));
    }

    void namesThatNameNobodyAreRefused()
    {
        QTemporaryDir directory;
        join(savedWorld(directory));
        const QString zeroWidth = QString(QChar(0x200B));
        for (const QString &command : {u"/op ."_s, u"/ban \"\""_s, QString(u"/whitelist add "_s + zeroWidth), u"/deop ..."_s, u"/unban -"_s,
                                       u"/kick !"_s}) {
            chat(command);
            QVERIFY2(lastLine().startsWith(u"Not a valid player name: "_s), qPrintable(command + u" -> "_s + lastLine()));
        }
        QCOMPARE(m_client->chatKeys.constLast().key, u"Not a valid player name: %1"_s);
        QVERIFY(!QFile::exists(directory.filePath(u"access.json"_s)));

        // A joining player of such a name is "Player", like one without a name.
        QCOMPARE(QcProfileValidation::playerName(u"..."_s), u"Player"_s);
        QCOMPARE(QcProfileValidation::validPlayerName(u" ?! "_s), std::nullopt);
        QCOMPARE(QcProfileValidation::validPlayerName(u"R2"_s), std::optional<QString>(u"R2"_s));
        QCOMPARE(QcProfileValidation::validPlayerName(u"\u2603"_s), std::optional<QString>(u"\u2603"_s));
    }

    void aSecondBanReplacesTheReason()
    {
        QTemporaryDir directory;
        join(savedWorld(directory));
        chat(u"/ban Griefer spam"_s);
        chat(u"/ban griefer broke the spawn"_s);
        QCOMPARE(lastLine(), u"Banned griefer"_s);
        const QJsonArray bans = accessFile(directory).value(u"bans"_s).toArray();
        QCOMPARE(bans.size(), 1);
        QCOMPARE(bans.first().toObject().value(u"reason"_s).toString(), u"broke the spawn"_s);
    }

    void fullListsRefuseNewNames()
    {
        QTemporaryDir directory;
        QJsonArray names;
        QJsonArray bans;
        for (int i = 0; i < QcAccessList::maxEntries; ++i) {
            names.append(u"Name%1"_s.arg(i));
            bans.append(QJsonObject{{u"name"_s, u"Banned%1"_s.arg(i)}});
        }
        {
            QFile file(directory.filePath(u"access.json"_s));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(QJsonObject{{u"format"_s, 1}, {u"operators"_s, names}, {u"bans"_s, bans},
                                                 {u"whitelist"_s, QJsonObject{{u"enabled"_s, false}, {u"names"_s, names}}}})
                           .toJson());
        }
        join(savedWorld(directory));
        for (const QString &command : {u"/op Newcomer"_s, u"/ban Newcomer"_s, u"/whitelist add Newcomer"_s}) {
            chat(command);
            QVERIFY2(lastLine() == u"The list is full (at most 4096 names)"_s, qPrintable(command + u" -> "_s + lastLine()));
        }
        // Names already listed still fit: a new reason, removing and adding again.
        chat(u"/ban Banned7 again"_s);
        QCOMPARE(lastLine(), u"Banned Banned7"_s);
        chat(u"/whitelist remove Name3"_s);
        chat(u"/whitelist add Newcomer"_s);
        QCOMPARE(lastLine(), u"Newcomer is on the whitelist"_s);
    }

    void aBanHoldsForOperatorsToo()
    {
        // Operators pass the whitelist, not a ban; only the host always gets in.
        QTemporaryDir directory;
        join(savedWorld(directory));
        chat(u"/op Helper"_s);
        chat(u"/ban Helper"_s);
        QCOMPARE(connect(u"Helper"_s)->disconnectReason, u"You are banned from this world"_s);
        QVERIFY(connect(u"Helper"_s, QcServer::ClientOrigin::Local)->disconnectReason.isEmpty());
    }

    void invalidEntriesAreLeftOut()
    {
        QTemporaryDir directory;
        QJsonArray names{u"Good"_s, u""_s, 5, u"  padded  "_s, QString(40, u'x')};
        QJsonArray bans{QJsonObject{{u"name"_s, u"Bad"_s}, {u"reason"_s, QString(500, u'r')}}, QJsonObject{{u"reason"_s, u"no name"_s}}};
        {
            QFile file(directory.filePath(u"access.json"_s));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(QJsonObject{{u"format"_s, 1}, {u"operators"_s, names}, {u"bans"_s, bans}}).toJson());
        }
        QcAccessList access;
        QStringList warnings;
        access.open(directory.path(), {}, warnings);
        QCOMPARE(access.operators(), QStringList{u"Good"_s});
        QCOMPARE(access.bans().size(), 1);
        QCOMPARE(access.ban(u"bad"_s)->reason.size(), QcAccessList::maxReasonLength);
        QCOMPARE(warnings.size(), 5);
        QVERIFY(!access.whitelistEnabled());
        QVERIFY(!QFile::exists(directory.filePath(u"access.json.damaged"_s)));
    }
};

QTEST_GUILESS_MAIN(TestPlayerManagement)
#include "test_player_management.moc"
