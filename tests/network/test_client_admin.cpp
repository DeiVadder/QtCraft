// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/admin/QcAdminCompletion.h"
#include "clientcore/control/QcControlInterface.h"
#include "clientcore/session/QcClientSession.h"
#include "common/protocol/QcChatText.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

} // namespace

// The client's side of the admin channel (decision E-848): QcClientAdmin in a session against a
// real server, completion of command lines and the control interface's admin commands.
class TestClientAdmin : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;

    void start(const QStringList &operators)
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.seed = 1;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        config.operators = operators;
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, QcWorldGenerators::create(config.generator, m_content, 1, {}, QcGeneratorVersion::current));
        QVERIFY(m_host->startError().isEmpty());
        m_session = std::make_unique<QcClientSession>(m_host->connectClient(), u"Player"_s);
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
    }

    // Waits for the answer to request `id`.
    std::optional<QcMsg::AdminResult> answer(quint32 id)
    {
        if (!QTest::qWaitFor([&] { return m_session->admin().result(id).has_value(); }, timeoutMs)) {
            return std::nullopt;
        }
        return m_session->admin().result(id);
    }

private slots:
    void cleanup()
    {
        if (m_session) {
            m_session->disconnectFromServer();
            m_session.reset();
        }
        if (m_host) {
            QVERIFY(m_host->shutdown().has_value());
            m_host.reset();
        }
    }

    // The server tells the player of his own world that he is an operator, without being asked:
    // the pause menu offers "Manage server" (decision E-863).
    void theHostHearsHeIsAnOperator()
    {
        start({});
        QTRY_VERIFY_WITH_TIMEOUT(m_session->admin().operatorRights(), timeoutMs);
        m_session->disconnectFromServer();
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->admin().operatorRights(), timeoutMs);
    }

    void followsTheStatusAndPlayers()
    {
        start({});
        QcClientAdmin &admin = m_session->admin();
        QSignalSpy statuses(&admin, &QcClientAdmin::statusChanged);
        admin.follow(QcMsg::AdminSubscribe::status | QcMsg::AdminSubscribe::chat);
        QTRY_VERIFY_WITH_TIMEOUT(statuses.size() >= 2, timeoutMs);
        QVERIFY(admin.status());
        QCOMPARE(admin.status()->sample.players, 1u);
        QVERIFY(!admin.history().seconds.isEmpty());
        QCOMPARE(admin.players().size(), 1);
        QCOMPARE(admin.players().first().name, u"Player"_s);
        // The session's tick thread answers the pings.
        QTRY_VERIFY_WITH_TIMEOUT(!admin.players().isEmpty() && admin.players().first().pingMs >= 0, timeoutMs);

        m_session->sendChat(u"hi there"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!admin.chat().isEmpty() && admin.chat().last().sender == u"Player"_s, timeoutMs);
    }

    void commandsAnswerWithTheirLines()
    {
        start({});
        QcClientAdmin &admin = m_session->admin();
        const std::optional<QcMsg::AdminResult> result = answer(admin.runCommand(u"/time set night"_s));
        QVERIFY(result && result->ok);
        QVERIFY(QcClientAdmin::display(*result).join(u' ').contains(u"Time is now"_s));

        QSignalSpy lists(&admin, &QcClientAdmin::listsChanged);
        QVERIFY(answer(admin.request(QcClientAdmin::Kind::WhitelistAdd, {u"Bob"_s})));
        QTRY_VERIFY_WITH_TIMEOUT(admin.lists().whitelist.contains(u"Bob"_s), timeoutMs);
    }

    void completesCommandLines()
    {
        start({});
        QcClientAdmin &admin = m_session->admin();
        QVERIFY(answer(admin.request(QcClientAdmin::Kind::Catalogue)));
        QVERIFY(!admin.catalogue().commands.isEmpty());

        const QcAdminCompletion::Sources sources{admin.catalogue(), {u"Player"_s, u"Bob"_s}, m_content.get()};
        QVERIFY(QcAdminCompletion::complete(u"/gamem"_s, sources).contains(u"/gamemode"_s));
        QVERIFY(QcAdminCompletion::complete(u"/gamemode c"_s, sources).contains(u"/gamemode creative"_s));
        QCOMPARE(QcAdminCompletion::complete(u"/kick B"_s, sources), QStringList{u"/kick Bob"_s});
        QVERIFY(QcAdminCompletion::complete(u"/give diamond_sw"_s, sources).contains(u"/give qtcraft:diamond_sword"_s));
        QVERIFY(QcAdminCompletion::complete(u"/time set "_s, sources).contains(u"/time set night"_s));
        QVERIFY(QcAdminCompletion::complete(u"/nonsense "_s, sources).isEmpty());
        const QList<QcCommandSyntax::Parameter> next = QcAdminCompletion::parameters(u"/tp "_s, admin.catalogue());
        QVERIFY(std::ranges::any_of(next, [](const auto &p) { return p.type == QcCommandSyntax::Parameter::Type::Coordinate; }));
        QVERIFY(std::ranges::any_of(next, [](const auto &p) { return p.type == QcCommandSyntax::Parameter::Type::Player; }));
    }

    void refusalsReachTheScreen()
    {
        // Without a server: what the client does with the server's answers.
        QcClientAdmin admin;
        QList<QcMessage> sent;
        admin.setSender([&sent](const QcMessage &message) { sent.append(message); });
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"not sent: no server has welcomed"_s));
        QCOMPARE(admin.request(QcClientAdmin::Kind::Players), 0u);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"include unknown ones"_s));
        admin.follow(QcMsg::AdminSubscribe::status | 64);
        QCOMPARE(admin.topics(), QcMsg::AdminSubscribe::status);
        QVERIFY(sent.isEmpty());
        admin.welcomed();
        QCOMPARE(sent.size(), 1);
        QVERIFY(std::holds_alternative<QcMsg::AdminSubscribe>(sent.first()));

        // Other refusals keep the subscription and say why.
        QSignalSpy refusals(&admin, &QcClientAdmin::refused);
        const quint32 stop = admin.request(QcClientAdmin::Kind::Stop);
        QVERIFY(admin.receive(QcMsg::AdminResult{stop, false, {}, QcMsg::AdminResult::Refusal::Confirmation}));
        QCOMPARE(admin.lastRefusal(), QcClientAdmin::Refusal::Confirmation);
        QCOMPARE(refusals.last().at(0).value<QcClientAdmin::Refusal>(), QcClientAdmin::Refusal::Confirmation);
        QCOMPARE(admin.topics(), QcMsg::AdminSubscribe::status);

        // Missing rights on any request end the subscription here too, so following the same topics
        // after the rights came back asks the server again.
        const quint32 id = admin.request(QcClientAdmin::Kind::Players);
        QVERIFY(id != 0);
        QVERIFY(admin.receive(QcMsg::AdminResult{id, false, {QcChatText::message(QT_TRANSLATE_NOOP("QcServerMessages", "Only operators can use the server administration"))}, QcMsg::AdminResult::Refusal::Rights}));
        QCOMPARE(refusals.size(), 2);
        QCOMPARE(admin.lastRefusal(), QcClientAdmin::Refusal::Rights);
        QVERIFY(!admin.result(id)->ok);
        QCOMPARE(admin.topics(), quint8(0));
        admin.follow(QcMsg::AdminSubscribe::status);
        QCOMPARE(sent.size(), 4);
        QVERIFY(std::holds_alternative<QcMsg::AdminSubscribe>(sent.last()));
        QVERIFY(admin.receive(QcMsg::AdminResult{0, true, {}}));
        QCOMPARE(admin.lastRefusal(), QcClientAdmin::Refusal::None);
        QVERIFY(!admin.receive(QcMsg::ChatMessage{}));

        // The status right after the history does not repeat its sample.
        QcMsg::AdminSample sample;
        sample.timeMs = 5;
        QVERIFY(admin.receive(QcMsg::AdminHistory{{sample}, {}}));
        QcMsg::AdminStatus status;
        status.sample = sample;
        QVERIFY(admin.receive(status));
        QCOMPARE(admin.history().seconds.size(), 1);
        admin.closed();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"not sent"_s));
        QCOMPARE(admin.request(QcClientAdmin::Kind::Players), 0u);
    }

    void theControlInterfaceUsesTheChannel()
    {
        start({});
        QBuffer input;
        QBuffer output;
        QVERIFY(input.open(QIODevice::ReadOnly));
        QVERIFY(output.open(QIODevice::ReadWrite));
        QcControlInterface control(m_session.get(), &input, &output);
        const auto run = [&](const QJsonObject &request) {
            output.buffer().clear();
            output.seek(0);
            control.submit(request);
            if (!QTest::qWaitFor([&] { return control.isIdle() && output.buffer().contains('\n'); }, timeoutMs)) {
                return QJsonObject{{u"ok"_s, false}, {u"error"_s, u"no answer"_s}};
            }
            return QJsonDocument::fromJson(output.buffer().trimmed()).object();
        };

        const QJsonObject rule = run({{u"cmd"_s, u"admin"_s}, {u"request"_s, u"gamerule"_s}, {u"args"_s, QJsonArray{u"keepInventory"_s, u"false"_s}}});
        QVERIFY2(rule.value(u"ok"_s).toBool(), qPrintable(rule.value(u"error"_s).toString()));
        const QJsonObject settings = run({{u"cmd"_s, u"admin"_s}, {u"request"_s, u"settings"_s}});
        const QJsonArray rules = settings.value(u"settings"_s).toObject().value(u"rules"_s).toArray();
        const auto keep = std::ranges::find_if(rules, [](const QJsonValue &v) { return v.toObject().value(u"key"_s) == u"keepInventory"_s; });
        QVERIFY(keep != rules.end());
        QCOMPARE((*keep).toObject().value(u"value"_s).toInteger(), 0);

        const QJsonObject catalogue = run({{u"cmd"_s, u"admin"_s}, {u"request"_s, u"catalogue"_s}});
        QVERIFY(catalogue.value(u"catalogue"_s).toObject().value(u"commands"_s).toArray().size() > 20);
        QVERIFY(!run({{u"cmd"_s, u"admin"_s}, {u"request"_s, u"nope"_s}}).value(u"ok"_s).toBool());

        QVERIFY(run({{u"cmd"_s, u"adminFollow"_s}, {u"topics"_s, QJsonArray{u"status"_s}}}).value(u"ok"_s).toBool());
        const QJsonObject status = run({{u"cmd"_s, u"adminStatus"_s}});
        QCOMPARE(status.value(u"status"_s).toObject().value(u"sample"_s).toObject().value(u"players"_s).toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(TestClientAdmin)
#include "test_client_admin.moc"
