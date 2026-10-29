// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcCommandSyntax.h"
#include "network/QcAdminTestFixture.h"

#include <QSignalSpy>

using namespace Qt::StringLiterals;

// The admin channel on the server (decision E-848): rights on every message, the actions as the
// console's commands, status with history, chat, the command catalogue and the access lists.
class TestAdminChannel : public QcAdminTestFixture
{
    Q_OBJECT

    // A second raw client on the same server.
    std::unique_ptr<RawClient> connect(const QString &name)
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto client = std::make_unique<RawClient>(std::move(clientEnd));
        client->hello(name);
        QcTestUtil::pump();
        ticks(2);
        return client;
    }

    // The access lists as a comparable text: names and reasons, without dates and who did it.
    static QString listsText(const QcMsg::AdminLists &lists)
    {
        QStringList bans;
        for (const QcMsg::AdminLists::Ban &ban : lists.bans) {
            bans.append(ban.name + u'=' + ban.reason);
        }
        return u"%1|%2|%3|%4"_s.arg(lists.whitelistEnabled).arg(lists.operators.join(u','), lists.whitelist.join(u','), bans.join(u','));
    }

    // The answers to requests, without the notices of the rights (number 0).
    QList<QcMsg::AdminResult> answers() const
    {
        QList<QcMsg::AdminResult> found = received<QcMsg::AdminResult>();
        found.removeIf([](const QcMsg::AdminResult &result) { return result.id == 0; });
        return found;
    }

    QList<QcMsg::AdminResult> rightsNotices() const
    {
        QList<QcMsg::AdminResult> found = received<QcMsg::AdminResult>();
        found.removeIf([](const QcMsg::AdminResult &result) { return result.id != 0; });
        return found;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void aPlayerWithoutRightsIsRefused()
    {
        join(notOperator());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused admin players of Raw: not an operator"_s));
        const std::optional<QcMsg::AdminResult> result = ask(Kind::Players);
        QVERIFY(result);
        QVERIFY(!result->ok);
        QVERIFY(texts(*result).join(u' ').contains(u"Only operators"_s));
        QVERIFY(received<QcMsg::AdminPlayers>().isEmpty());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused admin subscribe of Raw"_s));
        follow(QcMsg::AdminSubscribe::all);
        ticks(40);
        QVERIFY(received<QcMsg::AdminStatus>().isEmpty());
        QVERIFY(received<QcMsg::AdminHistory>().isEmpty());

        // A command line through the channel does not get around the check either.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused admin command of Raw"_s));
        QVERIFY(!ask(Kind::Command, {u"/op Raw"_s})->ok);
        QVERIFY(!m_server->config().operators.contains(u"Raw"_s));
        QCOMPARE(console(u"op"_s).join(u' ').contains(u"Raw"_s), false);
    }

    void deopEndsTheChannelAtOnce()
    {
        join();
        follow(QcMsg::AdminSubscribe::status | QcMsg::AdminSubscribe::chat);
        ticks(25);
        QVERIFY(!received<QcMsg::AdminStatus>().isEmpty());
        const qsizetype before = received<QcMsg::AdminStatus>().size();

        // Operators of the configuration lose their rights with /deop until a restart.
        console(u"deop Raw"_s);
        ticks(1);
        const std::optional<QcMsg::AdminResult> ended = latest<QcMsg::AdminResult>();
        QVERIFY(ended);
        QCOMPARE(ended->id, 0u);
        QVERIFY(!ended->ok);
        ticks(40);
        QCOMPARE(received<QcMsg::AdminStatus>().size(), before);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused admin lists of Raw"_s));
        QVERIFY(!ask(Kind::Lists)->ok);
        QVERIFY(received<QcMsg::AdminLists>().isEmpty());
    }

    void aRefusalBeforeTheTickEndsTheSubscription()
    {
        join();
        follow(QcMsg::AdminSubscribe::status);
        QCOMPARE(received<QcMsg::AdminHistory>().size(), 1);

        // Rights lost and a request before the next tick: the subscription ends with number 0 too.
        console(u"deop Raw"_s);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused admin players of Raw"_s));
        const std::optional<QcMsg::AdminResult> refused = ask(Kind::Players);
        QVERIFY(refused && !refused->ok);
        QCOMPARE(refused->refusal, QcMsg::AdminResult::Refusal::Rights);
        QVERIFY(std::ranges::any_of(received<QcMsg::AdminResult>(), [](const auto &result) {
            return result.id == 0 && result.refusal == QcMsg::AdminResult::Refusal::Rights;
        }));

        // Rights back: following the same topics starts anew.
        console(u"op Raw"_s);
        follow(QcMsg::AdminSubscribe::status);
        QCOMPARE(received<QcMsg::AdminHistory>().size(), 2);
        const qsizetype before = received<QcMsg::AdminStatus>().size();
        ticks(20);
        QVERIFY(received<QcMsg::AdminStatus>().size() > before);
    }

    void theLocalPlayerIsNotRateLimited()
    {
        m_origin = QcServer::ClientOrigin::Local;
        join();
        for (int i = 0; i < 60; ++i) {
            m_client->connection.send(QcMsg::AdminRequest{static_cast<quint32>(1000 + i), Kind::Players, {}});
        }
        QcTestUtil::pump();
        m_origin = QcServer::ClientOrigin::Remote;
        QCOMPARE(answers().size(), 60);
    }

    void longTextsArriveWhole()
    {
        // "/say " and 256 characters: longer than a chat line, still whole.
        join();
        const QString text(QcMsg::AdminLimits::maxText, u'x');
        QVERIFY(ask(Kind::Chat, {text})->ok);
        ticks(1);
        QVERIFY(m_client->chatLines.last().endsWith(text));
        QCOMPARE(m_client->chatKeys.last().arguments.first(), text);
    }

    // Every player hears whether he is an operator (decision E-863), once and again when it
    // changes; a player who never was one hears nothing.
    void playersHearTheirRights()
    {
        join();
        ticks(1);
        QCOMPARE(rightsNotices().size(), 1);
        QVERIFY(rightsNotices().constFirst().ok);
        ticks(5);
        QCOMPARE(rightsNotices().size(), 1);

        console(u"deop Raw"_s);
        ticks(2);
        QCOMPARE(rightsNotices().size(), 2);
        QCOMPARE(rightsNotices().constLast().refusal, QcMsg::AdminResult::Refusal::Rights);
        console(u"op Raw"_s);
        ticks(2);
        QCOMPARE(rightsNotices().size(), 3);
        QVERIFY(rightsNotices().constLast().ok);

        // A second player who is no operator gets no notice at all.
        std::unique_ptr<RawClient> guest = connect(u"Guest"_s);
        ticks(3);
        QVERIFY(std::ranges::none_of(guest->admin, [](const QcMessage &message) { return std::holds_alternative<QcMsg::AdminResult>(message); }));
    }

    void deopOfOneselfAnswersAndThenRefuses()
    {
        join();
        const std::optional<QcMsg::AdminResult> deop = ask(Kind::Deop, {u"Raw"_s});
        QVERIFY(deop && deop->ok);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused admin players of Raw"_s));
        QVERIFY(!ask(Kind::Players)->ok);
    }

    void actionsEqualTheConsoleCommands()
    {
        // The same changes once through the admin channel and once through the console.
        join();
        const QList<std::pair<Kind, QStringList>> actions{{Kind::Ban, {u"Mallory"_s, u"griefing the spawn"_s}},
                                                          {Kind::Ban, {u"Eve"_s}},
                                                          {Kind::Unban, {u"Eve"_s}},
                                                          {Kind::Op, {u"Alice"_s}},
                                                          {Kind::WhitelistAdd, {u"Bob"_s}},
                                                          {Kind::WhitelistAdd, {u"Carol"_s}},
                                                          {Kind::WhitelistRemove, {u"Carol"_s}},
                                                          {Kind::Whitelist, {u"on"_s}}};
        for (const auto &[kind, args] : actions) {
            const std::optional<QcMsg::AdminResult> result = ask(kind, args);
            QVERIFY(result && result->ok);
            QVERIFY2(!result->lines.isEmpty(), "every action answers with the command's reply");
        }
        QVERIFY(ask(Kind::Lists)->ok);
        const QString viaAdmin = listsText(*latest<QcMsg::AdminLists>());

        cleanup();
        join();
        for (const QString &line : {u"ban Mallory griefing the spawn"_s, u"ban Eve"_s, u"unban Eve"_s, u"op Alice"_s, u"whitelist add Bob"_s,
                                    u"whitelist add Carol"_s, u"whitelist remove Carol"_s, u"whitelist on"_s}) {
            console(line);
        }
        QVERIFY(ask(Kind::Lists)->ok);
        QCOMPARE(viaAdmin, listsText(*latest<QcMsg::AdminLists>()));
        QVERIFY(viaAdmin.contains(u"Mallory=griefing the spawn"_s));
        QVERIFY(viaAdmin.contains(u"Alice"_s));
    }

    void kickDisconnectsWithTheReason()
    {
        join();
        std::unique_ptr<RawClient> other = connect(u"Bob"_s);
        const std::optional<QcMsg::AdminResult> result = ask(Kind::Kick, {u"Bob"_s, u"too loud"_s});
        QVERIFY(result && result->ok);
        ticks(2);
        QVERIFY(other->disconnectReason.contains(u"too loud"_s));
        // Changes of the players answer with the lists and the players.
        QVERIFY(latest<QcMsg::AdminPlayers>());
        QVERIFY(latest<QcMsg::AdminLists>());
    }

    void wrongArgumentsAreRefused()
    {
        join();
        // A name with a space would shift the words of the command line.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused admin ban of Raw: wrong arguments"_s));
        QVERIFY(!ask(Kind::Ban, {u"Bob /op"_s, u"x"_s})->ok);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused admin whitelist of Raw: wrong arguments"_s));
        QVERIFY(!ask(Kind::Whitelist, {u"maybe"_s})->ok);
        QVERIFY(m_server->config().operators == QStringList{u"Raw"_s});
    }

    void statusComesEverySecondWithHistory()
    {
        join();
        // The history is recorded before anyone follows it.
        ticks(60);
        follow(QcMsg::AdminSubscribe::status);
        const std::optional<QcMsg::AdminHistory> history = latest<QcMsg::AdminHistory>();
        QVERIFY(history);
        QVERIFY(history->seconds.size() >= 2);
        QVERIFY(latest<QcMsg::AdminStatus>());
        QVERIFY(latest<QcMsg::AdminPlayers>());

        const qsizetype before = received<QcMsg::AdminStatus>().size();
        ticks(40);
        // One status a second of the players' time (20 ticks).
        QCOMPARE(received<QcMsg::AdminStatus>().size(), before + 2);
        const QcMsg::AdminStatus status = *latest<QcMsg::AdminStatus>();
        QCOMPARE(status.sample.players, 1u);
        QVERIFY(status.sample.loadedChunks > 0);
        QCOMPARE(status.entities[std::size_t(QcEntityKind::Player)], 1u);
        QCOMPARE(qRound(status.sample.ticksPerSecond), 20);
        QVERIFY(status.sample.tickP99Ms >= status.sample.tickMeanMs * 0.0f);
        QVERIFY(status.sample.tickMaxMs >= status.sample.tickP99Ms);
        QCOMPARE(status.protocol, QcProtocol::version);
        QCOMPARE(status.worldName, m_server->config().worldName);
        QVERIFY(status.uptimeMs > 0);

        // The players answer the pings: the list knows their round trip.
        ticks(120);
        QVERIFY(m_client->pings > 0);
        QVERIFY(latest<QcMsg::AdminPlayers>()->players.first().pingMs >= 0);
        QCOMPARE(latest<QcMsg::AdminPlayers>()->players.first().name, u"Raw"_s);
        QVERIFY(latest<QcMsg::AdminPlayers>()->players.first().operatorRights);
    }

    void minutesAreFoldedFromSeconds()
    {
        join();
        ticks(20 * 61);
        follow(QcMsg::AdminSubscribe::status);
        const QcMsg::AdminHistory history = *latest<QcMsg::AdminHistory>();
        QCOMPARE(history.minutes.size(), 1);
        QVERIFY(history.seconds.size() >= 60);
        QCOMPARE(qRound(history.minutes.first().ticksPerSecond), 20);
    }

    void chatHistoryAndLiveLines()
    {
        join();
        chat(u"before anyone follows"_s);
        follow(QcMsg::AdminSubscribe::chat);
        const QcMsg::AdminChat history = *latest<QcMsg::AdminChat>();
        QVERIFY(history.history);
        QCOMPARE(history.lines.size(), 1);
        QCOMPARE(history.lines.first().sender, u"Raw"_s);

        chat(u"hello"_s);
        ticks(1);
        const QcMsg::AdminChat live = *latest<QcMsg::AdminChat>();
        QVERIFY(!live.history);
        QCOMPARE(live.lines.size(), 1);
        QVERIFY(live.lines.first().message.text.contains(u"hello"_s));

        // A line as the server reaches every player and the log of the chat.
        QVERIFY(ask(Kind::Chat, {u"restart at noon"_s})->ok);
        ticks(1);
        QVERIFY(m_client->chatLines.last().contains(u"[Server] restart at noon"_s));
        QCOMPARE(latest<QcMsg::AdminChat>()->lines.last().sender, QString());
    }

    void commandsRunAsTheOperator()
    {
        join();
        const std::optional<QcMsg::AdminResult> result = ask(Kind::Command, {u"time set day"_s});
        QVERIFY(result && result->ok);
        QVERIFY(texts(*result).join(u' ').contains(u"Time is now"_s));
        // The replies come back in the answer, not in the chat.
        QVERIFY(!m_client->chatLines.join(u' ').contains(u"Time is now"_s));
        const std::optional<QcMsg::AdminResult> unknown = ask(Kind::Command, {u"/nonsense"_s});
        QVERIFY(texts(*unknown).join(u' ').contains(u"Unknown command"_s));
    }

    void theCatalogueHasEveryCommand()
    {
        join();
        QVERIFY(ask(Kind::Catalogue)->ok);
        const QcMsg::AdminCatalogue catalogue = *latest<QcMsg::AdminCatalogue>();
        const QList<const QcCommandSpec *> commands = m_server->commands().registry().commands();
        QVERIFY(catalogue.commands.size() >= commands.size());
        for (const QcCommandSpec *spec : commands) {
            const auto entry = std::ranges::find(catalogue.commands, spec->name, &QcMsg::AdminCatalogue::Command::name);
            QVERIFY2(entry != catalogue.commands.end(), qPrintable(spec->name));
            QCOMPARE(entry->usage, QString::fromUtf8(spec->usage));
            QCOMPARE(entry->description, QString::fromUtf8(spec->description));
            QCOMPARE(entry->permission, quint8(spec->permission));
            // Every usage gives forms; a command that needs words has a parameter in each.
            const QList<QcCommandSyntax::Form> forms = QcCommandSyntax::forms(entry->usage);
            QVERIFY2(!forms.isEmpty(), qPrintable(entry->usage));
            if (spec->minArguments > 0) {
                QVERIFY2(std::ranges::all_of(forms, [](const QcCommandSyntax::Form &form) { return !form.parameters.isEmpty(); }),
                         qPrintable(entry->usage));
            }
        }
        QVERIFY(std::ranges::any_of(catalogue.commands, [](const auto &command) { return command.name == u"say"_s; }));
    }

    void stopNeedsTheConfirmation()
    {
        join();
        QSignalSpy stops(m_server.get(), &QcServer::stopRequested);
        QVERIFY(!ask(Kind::Stop)->ok);
        QCOMPARE(stops.size(), 0);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Raw stops the server"_s));
        const std::optional<QcMsg::AdminResult> stopped = ask(Kind::Stop, {u"confirm"_s});
        QVERIFY(stopped && stopped->ok);
        QCOMPARE(stops.size(), 1);
    }

    void requestsAreRateLimited()
    {
        join();
        for (int i = 0; i < 60; ++i) {
            m_client->connection.send(QcMsg::AdminRequest{static_cast<quint32>(1000 + i), Kind::Players, {}});
        }
        QcTestUtil::pump();
        QVERIFY(answers().size() >= 15);
        QVERIFY(answers().size() <= 25);
    }

    void oversizedRequestsCloseTheConnection()
    {
        join();
        // Five arguments: the decoder refuses the frame before the server sees it.
        m_client->connection.send(QcMsg::AdminRequest{1, Kind::Command, {u"a"_s, u"b"_s, u"c"_s, u"d"_s, u"e"_s}});
        QcTestUtil::pump();
        ticks(2);
        QVERIFY(answers().isEmpty());
        QCOMPARE(m_server->clientCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestAdminChannel)
#include "test_admin_channel.moc"
