#include "common/core/QcCommandSyntax.h"
#include "common/diagnostics/QcMessageLog.h"
#include "common/protocol/QcProtocol.h"
#include "QcTestUtil.h"

#include <QDataStream>
#include <QLoggingCategory>
#include <QTest>

using namespace Qt::StringLiterals;
using Parameter = QcCommandSyntax::Parameter;

namespace {

Q_LOGGING_CATEGORY(lcAdminTest, "qtcraft.admintest")

// Encodes and decodes a message as `sender` sends it.
template<typename T>
QcResult<QcMessage> decoded(const T &message, QcProtocol::Peer sender)
{
    return QcProtocol::decode(QcProtocol::encode(QcMessage(message)), sender);
}

template<typename T>
T roundTrip(const T &message, QcProtocol::Peer sender = QcProtocol::Peer::Server)
{
    const QcResult<QcMessage> result = decoded(message, sender);
    if (!result || !std::holds_alternative<T>(*result)) {
        QcTestUtil::failCase(u"round trip failed: %1"_s.arg(result ? u"wrong type"_s : result.error()));
        return {};
    }
    return std::get<T>(*result);
}

QcMsg::AdminSample sample(qint64 time)
{
    return {time, 1.5f, 4.0f, 9.0f, 20.0f, 3, 400, 120, 512.0f, 12.5f, 3.25f};
}

// The names and kinds of the parameters of the form, "name:kind" each.
QStringList shape(const QcCommandSyntax::Form &form)
{
    static constexpr std::array<const char *, 3> kinds{"literal", "required", "optional"};
    QStringList parts;
    for (const Parameter &p : form.parameters) {
        parts.append(u"%1:%2:%3"_s.arg(p.name, QLatin1StringView(kinds[std::size_t(p.kind)]), QcCommandSyntax::typeName(p.type)));
    }
    return parts;
}

} // namespace

// The messages of the admin channel and their bounds, the parameters read from usage texts and
// the log lines kept for the channel (decision E-848).
class TestAdminProtocol : public QObject
{
    Q_OBJECT

private slots:
    void versionIsBumped()
    {
        // 66 is package 73.3 (decision E-839), 67 the admin channel (decision E-848).
        QCOMPARE(QcProtocol::version, 67u);
    }

    void eachSideSendsOnlyItsOwnMessages()
    {
        const auto index = [](const QcMessage &message) { return message.index(); };
        for (const QcMessage &clientMessage : {QcMessage(QcMsg::AdminSubscribe{}), QcMessage(QcMsg::AdminRequest{}), QcMessage(QcMsg::Pong{})}) {
            QVERIFY(QcProtocol::isSentBy(index(clientMessage), QcProtocol::Peer::Client));
            QVERIFY(!QcProtocol::isSentBy(index(clientMessage), QcProtocol::Peer::Server));
        }
        for (const QcMessage &serverMessage : {QcMessage(QcMsg::AdminResult{}), QcMessage(QcMsg::AdminStatus{}), QcMessage(QcMsg::AdminHistory{}),
                                               QcMessage(QcMsg::AdminPlayers{}), QcMessage(QcMsg::AdminChat{}), QcMessage(QcMsg::AdminCatalogue{}),
                                               QcMessage(QcMsg::AdminLists{}), QcMessage(QcMsg::AdminSettings{}), QcMessage(QcMsg::AdminLog{}),
                                               QcMessage(QcMsg::Ping{})}) {
            QVERIFY(QcProtocol::isSentBy(index(serverMessage), QcProtocol::Peer::Server));
            QVERIFY(!QcProtocol::isSentBy(index(serverMessage), QcProtocol::Peer::Client));
        }
        // A client cannot pose as the server with a report.
        QVERIFY(!decoded(QcMsg::AdminLists{}, QcProtocol::Peer::Client));
    }

    void requestsRoundTrip()
    {
        const QcMsg::AdminRequest request{7, QcMsg::AdminRequest::Kind::Ban, {u"Mallory"_s, u"griefing the spawn"_s}};
        const QcMsg::AdminRequest back = roundTrip(request, QcProtocol::Peer::Client);
        QCOMPARE(back.id, 7u);
        QCOMPARE(back.kind, QcMsg::AdminRequest::Kind::Ban);
        QCOMPARE(back.arguments, request.arguments);
        const QcMsg::AdminSubscribe subscribe = roundTrip(QcMsg::AdminSubscribe{QcMsg::AdminSubscribe::all, 2}, QcProtocol::Peer::Client);
        QCOMPARE(subscribe.topics, QcMsg::AdminSubscribe::all);
        QCOMPARE(subscribe.logLevel, quint8(2));
        QCOMPARE(roundTrip(QcMsg::Pong{42}, QcProtocol::Peer::Client).nonce, 42u);
        QCOMPARE(roundTrip(QcMsg::Ping{43}).nonce, 43u);
    }

    void requestBoundsAreChecked()
    {
        using Kind = QcMsg::AdminRequest::Kind;
        const auto client = QcProtocol::Peer::Client;
        QVERIFY(decoded(QcMsg::AdminRequest{1, Kind::Command, QStringList(4, u"a"_s)}, client));
        QVERIFY(!decoded(QcMsg::AdminRequest{1, Kind::Command, QStringList(5, u"a"_s)}, client));
        QVERIFY(decoded(QcMsg::AdminRequest{1, Kind::Command, {QString(QcMsg::AdminLimits::maxText, u'x')}}, client));
        // The encoder cuts texts to their bound; a frame written by hand with a longer one fails.
        QByteArray frame;
        QDataStream stream(&frame, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_8);
        stream << quint16(QcMessage(QcMsg::AdminRequest{}).index()) << quint32(1) << quint8(Kind::Command) << quint32(1)
               << QString(QcMsg::AdminLimits::maxText + 1, u'x');
        QVERIFY(!QcProtocol::decode(frame, client));
        QVERIFY(!decoded(QcMsg::AdminRequest{1, Kind(QcMsg::AdminRequest::kindCount), {}}, client));
        QVERIFY(!decoded(QcMsg::AdminSubscribe{8, 1}, client));
        QVERIFY(!decoded(QcMsg::AdminSubscribe{1, QcMsg::AdminLogLine::levelCount}, client));
    }

    void reportsRoundTrip()
    {
        QcMsg::AdminStatus status;
        status.sample = sample(1000);
        status.phaseMeanMs[3] = 0.75f;
        status.entities[std::size_t(QcEntityKind::Creature)] = 17;
        status.worldName = u"Welt"_s;
        status.seed = -42;
        status.version = u"qtcraft-headless 0.1.0"_s;
        status.protocol = QcProtocol::version;
        QCOMPARE(roundTrip(status), status);

        const QcMsg::AdminHistory history = roundTrip(QcMsg::AdminHistory{{sample(1), sample(2)}, {sample(60)}});
        QCOMPARE(history.seconds.size(), 2);
        QCOMPARE(history.minutes.first(), sample(60));

        QcMsg::AdminPlayers::Entry player{3, u"Ann"_s, 12, {1.5, 64.0, -3.0}, QcGameMode::Creative, true, false, 5000, u"10.0.0.2:5123"_s};
        QCOMPARE(roundTrip(QcMsg::AdminPlayers{{player}}).players.first(), player);

        QcMsg::AdminSettings settings;
        settings.rules.append({u"keepInventory"_s, u"Players"_s, u"d"_s, QcMsg::AdminSettings::Type::Boolean, 0, 1, 1, 0});
        settings.limits.append({u"summonLimit"_s, u"Creatures"_s, u"d"_s, QcMsg::AdminSettings::Type::Integer, 0, 4096, 64, 5});
        settings.limitsFromServer = true;
        const QcMsg::AdminSettings settingsBack = roundTrip(settings);
        QCOMPARE(settingsBack.rules, settings.rules);
        QCOMPARE(settingsBack.limits, settings.limits);
        QVERIFY(settingsBack.limitsFromServer);
    }

    void listsChatLogAndCatalogueRoundTrip()
    {
        const QcMsg::AdminLists lists = roundTrip(QcMsg::AdminLists{true, {u"Op"_s}, {u"Bob"_s}, {{u"Eve"_s, u"spam"_s, u"Server"_s, 99}}});
        QVERIFY(lists.whitelistEnabled);
        QCOMPARE(lists.bans.first().reason, u"spam"_s);
        QCOMPARE(lists.bans.first().sinceMs, 99);

        const QcMsg::AdminChat chat = roundTrip(QcMsg::AdminChat{true, {{5, u"Ann"_s, {u"<Ann> hi"_s, {}, {}}}}});
        QVERIFY(chat.history);
        QCOMPARE(chat.lines.first().message.text, u"<Ann> hi"_s);

        const QcMsg::AdminLog log = roundTrip(QcMsg::AdminLog{true, {{1, QcMsg::AdminLogLine::Level::Warning, u"qtcraft.server"_s, u"slow"_s}}});
        QCOMPARE(log.lines.first().level, QcMsg::AdminLogLine::Level::Warning);

        const QcMsg::AdminCatalogue catalogue =
            roundTrip(QcMsg::AdminCatalogue{{{u"tp"_s, {u"teleport"_s}, 1, true, false, u"Usage: /tp <player>"_s, u"Teleports."_s}}});
        QCOMPARE(catalogue.commands.first().aliases, QStringList{u"teleport"_s});

        const QcMsg::AdminResult result = roundTrip(QcMsg::AdminResult{3, true, {{u"World saved"_s, u"World saved"_s, {}}}});
        QCOMPARE(result.lines.size(), 1);
        // Replies keep arguments longer than a chat line (a /say of 256 characters).
        const QString longArgument(QcMsg::AdminLimits::maxText + 100, u'y');
        const QcMsg::AdminResult refused =
            roundTrip(QcMsg::AdminResult{4, false, {{u"x"_s, u"%1"_s, {longArgument}}}, QcMsg::AdminResult::Refusal::TooSoon});
        QCOMPARE(refused.refusal, QcMsg::AdminResult::Refusal::TooSoon);
        QCOMPARE(refused.lines.first().arguments.first(), longArgument);
    }

    void reportBoundsAreChecked()
    {
        QcMsg::AdminResult tooMany{1, true, QList<QcMsg::ChatMessage>(QcMsg::AdminLimits::maxLines + 1)};
        QVERIFY(!decoded(tooMany, QcProtocol::Peer::Server));
        QcMsg::AdminHistory longHistory{QList<QcMsg::AdminSample>(QcMsg::AdminLimits::maxHistory + 1), {}};
        QVERIFY(!decoded(longHistory, QcProtocol::Peer::Server));
        QcMsg::AdminStatus broken;
        broken.sample.tickMeanMs = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!decoded(broken, QcProtocol::Peer::Server));
        QcMsg::AdminSettings inverted;
        inverted.rules.append({u"x"_s, {}, {}, QcMsg::AdminSettings::Type::Integer, 10, 1, 0, 0});
        QVERIFY(!decoded(inverted, QcProtocol::Peer::Server));
    }

    void usageTextsGiveTheirForms()
    {
        const QList<QcCommandSyntax::Form> tp =
            QcCommandSyntax::forms(u"Usage: /tp <x> <y> <z>, /tp <player>, /tp <player> <x> <y> <z> or /tp <player> <target player>"_s);
        QCOMPARE(tp.size(), 4);
        QCOMPARE(shape(tp[0]), (QStringList{u"x:required:coordinate"_s, u"y:required:coordinate"_s, u"z:required:coordinate"_s}));
        QCOMPARE(shape(tp[3]), (QStringList{u"player:required:player"_s, u"target player:required:player"_s}));

        const QList<QcCommandSyntax::Form> whitelist = QcCommandSyntax::forms(u"Usage: /whitelist <on|off|list|add <player>|remove <player>>"_s);
        QCOMPARE(whitelist.size(), 1);
        QCOMPARE(whitelist[0].parameters[0].type, Parameter::Type::Choice);
        QCOMPARE(whitelist[0].parameters[0].choices, (QStringList{u"on"_s, u"off"_s, u"list"_s, u"add"_s, u"remove"_s}));

        const QList<QcCommandSyntax::Form> regenerate = QcCommandSyntax::forms(u"Usage: /regenerate [<x> <z> [<x2> <z2>]]"_s);
        QCOMPARE(shape(regenerate[0]), (QStringList{u"x:optional:coordinate"_s, u"z:optional:coordinate"_s, u"x2:optional:coordinate"_s,
                                                     u"z2:optional:coordinate"_s}));

        const QList<QcCommandSyntax::Form> setblock = QcCommandSyntax::forms(u"Usage: /setblock <x> <y> <z> <block>[states] [creature]"_s);
        QCOMPARE(shape(setblock[0]).mid(3), (QStringList{u"block:required:block"_s, u"states:optional:word"_s, u"creature:optional:creature"_s}));

        const QList<QcCommandSyntax::Form> gamerule =
            QcCommandSyntax::forms(u"Usage: /gamerule <keepInventory|daylightCycle> [true|false] | spawnChunkRadius [0-16]"_s);
        QCOMPARE(gamerule.size(), 2);
        QCOMPARE(gamerule[1].parameters[0].name, u"spawnChunkRadius"_s);
        QCOMPARE(gamerule[1].parameters[1].range, (std::pair<qint64, qint64>{0, 16}));

        const QList<QcCommandSyntax::Form> locate = QcCommandSyntax::forms(u"Usage: /locate village|<structure>"_s);
        QCOMPARE(locate[0].parameters[0].choices, QStringList{u"village"_s});

        const QList<QcCommandSyntax::Form> kick = QcCommandSyntax::forms(u"Usage: /kick <player> [reason]"_s);
        QCOMPARE(shape(kick[0]), (QStringList{u"player:required:player"_s, u"reason:optional:text"_s}));
    }

    void nextFollowsTheTypedWords()
    {
        const QList<QcCommandSyntax::Form> time = QcCommandSyntax::forms(u"Usage: /time set <day|night|seconds> or /time add <seconds>"_s);
        QCOMPARE(QcCommandSyntax::next(time, {}).size(), 2);
        const QList<Parameter> afterSet = QcCommandSyntax::next(time, {u"set"_s});
        QCOMPARE(afterSet.size(), 1);
        QCOMPARE(afterSet[0].choices, (QStringList{u"day"_s, u"night"_s, u"seconds"_s}));
        QCOMPARE(QcCommandSyntax::next(time, {u"add"_s})[0].type, Parameter::Type::Integer);
        QVERIFY(QcCommandSyntax::next(time, {u"set"_s, u"day"_s}).isEmpty());
        // Free text takes the rest of the line.
        QVERIFY(QcCommandSyntax::next(QcCommandSyntax::forms(u"Usage: /say <message>"_s), {u"a"_s, u"b"_s}).isEmpty());
    }

    void textsOutsideTheNotationStillParse()
    {
        QCOMPARE(QcCommandSyntax::forms(u"teleports you home"_s).size(), 1);
        QCOMPARE(QcCommandSyntax::forms(QString()).size(), 1);
        QVERIFY(!QcCommandSyntax::forms(u"Usage: /broken <unclosed [x"_s).isEmpty());
    }

    void theLogKeepsItsNewestLines()
    {
        QcMessageLog::installWithoutFile();
        const quint64 start = QcMessageLog::latestSequence();
        for (int i = 0; i < QcMessageLog::recentLimit + 50; ++i) {
            qCInfo(lcAdminTest).noquote() << u"line %1"_s.arg(i);
        }
        QTest::ignoreMessage(QtWarningMsg, "warned");
        qCWarning(lcAdminTest) << "warned";
        QCOMPARE(QcMessageLog::latestSequence(), start + quint64(QcMessageLog::recentLimit) + 51);

        const QList<QcMessageLog::RecentLine> all = QcMessageLog::recent(0, 100'000).lines;
        QCOMPARE(all.size(), QcMessageLog::recentLimit);
        QCOMPARE(all.last().text, u"warned"_s);
        QCOMPARE(all[all.size() - 2].text, u"line %1"_s.arg(QcMessageLog::recentLimit + 49));

        const QList<QcMessageLog::RecentLine> warnings = QcMessageLog::recent(2, 10).lines;
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(warnings.first().category, u"qtcraft.admintest"_s);
        QCOMPARE(QcMessageLog::recent(0, 3).lines.size(), 3);
        QCOMPARE(QcMessageLog::recent(0, 3).latest, QcMessageLog::latestSequence());
        QVERIFY(QcMessageLog::recent(0, 10, 0, {u"qtcraft.other"_s}).lines.isEmpty());
        QVERIFY(QcMessageLog::recent(0, 10, QcMessageLog::latestSequence()).lines.isEmpty());

        const QString longText(QcMessageLog::recentTextLimit * 2, u'y');
        qCInfo(lcAdminTest).noquote() << longText;
        QCOMPARE(QcMessageLog::recent(0, 1).lines.first().text.size(), QcMessageLog::recentTextLimit);
        QcMessageLog::uninstall();
    }
};

QTEST_GUILESS_MAIN(TestAdminProtocol)
#include "test_admin_protocol.moc"
