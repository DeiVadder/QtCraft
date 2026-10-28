#include "app/admin/QcAdminCommandList.h"
#include "app/admin/QcAdminController.h"
#include "app/admin/QcAdminLineModel.h"
#include "app/admin/QcAdminViews.h"
#include "common/core/QcCommandCategory.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

using Kind = QcMsg::AdminRequest::Kind;

QcMsg::ChatMessage plain(const QString &text)
{
    QcMsg::ChatMessage message;
    message.text = text;
    return message;
}

QcMsg::AdminSample sample(qint64 timeMs, float tps)
{
    QcMsg::AdminSample point;
    point.timeMs = timeMs;
    point.ticksPerSecond = tps;
    point.players = 2;
    return point;
}

} // namespace

// The glue between the admin channel and the "Manage server" screen (decision E-863):
// QcAdminController on a QcClientAdmin whose messages go to a list instead of a server. The screen
// follows the topics only while open, every action is the right request, answers and refusals
// become the notice or the command output, and the reports become the values QML shows.
class TestAdminController : public QObject
{
    Q_OBJECT

    std::unique_ptr<QcClientAdmin> m_admin;
    std::unique_ptr<QcAdminController> m_controller;
    QList<QcMessage> m_sent;

    template<typename T>
    QList<T> sent() const
    {
        QList<T> found;
        for (const QcMessage &message : m_sent) {
            if (const auto *typed = std::get_if<T>(&message)) {
                found.append(*typed);
            }
        }
        return found;
    }

    // Answers the latest request with `ok`, `lines` and `refusal`.
    void answer(bool ok, const QStringList &lines = {}, QcMsg::AdminResult::Refusal refusal = QcMsg::AdminResult::Refusal::None)
    {
        QcMsg::AdminResult result{sent<QcMsg::AdminRequest>().constLast().id, ok, {}, refusal};
        for (const QString &line : lines) {
            result.lines.append(plain(line));
        }
        QVERIFY(m_admin->receive(result));
    }

    static QcMsg::AdminCatalogue catalogue()
    {
        QcMsg::AdminCatalogue table;
        table.commands.append({u"time"_s, {}, 1, false, false, u"Usage: /time set <day|night>"_s, u"Sets the time."_s,
                               static_cast<quint8>(QcCommandCategory::World), {}});
        table.commands.append({u"tp"_s, {u"teleport"_s}, 1, true, false, u"Usage: /tp <player>"_s, u"Teleports."_s,
                               static_cast<quint8>(QcCommandCategory::Players), {}});
        table.commands.append({u"locate"_s, {}, 1, true, false, u"Usage: /locate <structure>"_s, u"Finds the nearest structure."_s,
                               static_cast<quint8>(QcCommandCategory::Finding), {u"village"_s, u"mineshaft"_s, u"dungeon"_s, u"desert_temple"_s}});
        table.commands.append({u"help"_s, {}, 0, false, false, u"Usage: /help [command]"_s, u"Lists the commands."_s,
                               static_cast<quint8>(QcCommandCategory::General), {}});
        table.commands.append({u"warp"_s, {}, 0, false, true, u"/warp <place>"_s, u"Jumps to a place."_s,
                               static_cast<quint8>(QcCommandCategory::Other), {}});
        return table;
    }

private slots:
    void init()
    {
        m_sent.clear();
        m_admin = std::make_unique<QcClientAdmin>();
        m_admin->setSender([this](const QcMessage &message) { m_sent.append(message); });
        m_admin->welcomed();
        m_controller = std::make_unique<QcAdminController>();
        m_controller->follow(m_admin.get(), {});
    }

    void followsOnlyWhileOpen()
    {
        QVERIFY(sent<QcMsg::AdminSubscribe>().isEmpty());
        m_controller->open();
        QVERIFY(m_controller->isActive());
        QCOMPARE(sent<QcMsg::AdminSubscribe>().size(), 1);
        QCOMPARE(sent<QcMsg::AdminSubscribe>().constLast().topics, QcMsg::AdminSubscribe::all);
        QList<Kind> asked;
        const QList<QcMsg::AdminRequest> requests = sent<QcMsg::AdminRequest>();
        for (const QcMsg::AdminRequest &request : requests) {
            asked.append(request.kind);
        }
        QCOMPARE(asked, (QList<Kind>{Kind::Catalogue, Kind::Lists, Kind::Settings}));

        // Opened again while open (the overlay moved between the screen's pages): nothing new.
        m_controller->open();
        QCOMPARE(sent<QcMsg::AdminSubscribe>().size(), 1);

        m_controller->close();
        QVERIFY(!m_controller->isActive());
        QCOMPARE(sent<QcMsg::AdminSubscribe>().constLast().topics, quint8(0));
    }

    void theServerSaysWhoIsAnOperator()
    {
        QSignalSpy changes(m_controller.get(), &QcAdminController::operatorRightsChanged);
        QVERIFY(!m_controller->operatorRights());
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, true, {}}));
        QVERIFY(m_controller->operatorRights());
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, false, {}, QcMsg::AdminResult::Refusal::Rights}));
        QVERIFY(!m_controller->operatorRights());
        QCOMPARE(changes.size(), 2);

        // The session went: no operator, the screen closed.
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, true, {}}));
        m_controller->open();
        m_admin.reset();
        QVERIFY(!m_controller->operatorRights());
        QVERIFY(!m_controller->isActive());
    }

    void actionsAreRequests()
    {
        m_controller->kick(u"Bob"_s, u" too loud "_s);
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().kind, Kind::Kick);
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().arguments, (QStringList{u"Bob"_s, u"too loud"_s}));
        m_controller->ban(u"Bob"_s, {});
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().arguments, QStringList{u"Bob"_s});
        m_controller->setWhitelist(true);
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().arguments, QStringList{u"on"_s});

        m_controller->backup(u"before-update"_s);
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().arguments, QStringList{u"before-update"_s});
        m_controller->stop();
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().kind, Kind::Stop);
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().arguments, QStringList{u"confirm"_s});
        const qsizetype before = sent<QcMsg::AdminRequest>().size();
        m_controller->sendChat(u"   "_s);
        QCOMPARE(sent<QcMsg::AdminRequest>().size(), before);
    }

    void answersAndRefusalsBecomeTheNotice()
    {
        m_controller->open();
        QSignalSpy notices(m_controller.get(), &QcAdminController::noticeChanged);
        m_controller->kick(u"Bob"_s, {});
        answer(true, {u"Kicked Bob"_s});
        QCOMPARE(m_controller->noticeKind(), QcAdminController::Notice::Done);
        QCOMPARE(m_controller->notice(), u"Kicked Bob"_s);

        m_controller->save();
        answer(false, {u"Wait"_s}, QcMsg::AdminResult::Refusal::TooSoon);
        QCOMPARE(m_controller->noticeKind(), QcAdminController::Notice::Refused);
        QCOMPARE(m_controller->notice(), QcAdminController::refusalText(QcClientAdmin::Refusal::TooSoon, {}));

        m_controller->op(u"nobody"_s);
        answer(false, {u"No player nobody"_s});
        QCOMPARE(m_controller->notice(), QcAdminController::refusalText(QcClientAdmin::Refusal::Arguments, u"No player nobody"_s));
        QVERIFY(m_controller->notice().contains(u"No player nobody"_s));

        // Lost rights end the subscription and say so, whatever was asked.
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, false, {}, QcMsg::AdminResult::Refusal::Rights}));
        QCOMPARE(m_controller->notice(), QcAdminController::refusalText(QcClientAdmin::Refusal::Rights, {}));
        QVERIFY(notices.size() >= 4);
        m_controller->clearNotice();
        QCOMPARE(m_controller->noticeKind(), QcAdminController::Notice::None);
    }

    void commandsShowInTheOutput()
    {
        m_controller->open();
        QcAdminLineModel *output = m_controller->output();
        m_controller->runCommand(u"  /time set day "_s);
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().kind, Kind::Command);
        QCOMPARE(sent<QcMsg::AdminRequest>().constLast().arguments, QStringList{u"/time set day"_s});
        answer(true, {u"Set the time to 1000"_s});
        QCOMPARE(output->count(), 2);
        QCOMPARE(output->lines()[0].text, u"> /time set day"_s);
        QCOMPARE(output->lines()[1].text, u"Set the time to 1000"_s);

        // A refusal with a reason shows as an error line; the notice stays for actions.
        m_controller->runCommand(u"/stop"_s);
        answer(false, {}, QcMsg::AdminResult::Refusal::Confirmation);
        QCOMPARE(output->count(), 4);
        QCOMPARE(output->lines()[3].level, int(QcMsg::AdminLogLine::Level::Critical));
        QCOMPARE(m_controller->noticeKind(), QcAdminController::Notice::None);

        m_controller->clearOutput();
        QCOMPARE(output->count(), 0);
    }

    void completionAndHelpComeFromTheCatalogue()
    {
        QVERIFY(m_admin->receive(catalogue()));
        QCOMPARE(m_controller->complete(u"/ti"_s), QStringList{u"/time"_s});
        QCOMPARE(m_controller->complete(u"/time set n"_s), QStringList{u"/time set night"_s});
        const QVariantMap help = m_controller->help(u"/teleport "_s);
        QCOMPARE(help.value(u"command"_s).toString(), u"tp"_s);
        QCOMPARE(help.value(u"next"_s).toString(), u"<player>"_s);
        QVERIFY(m_controller->help(u"/nonsense"_s).isEmpty());
        QCOMPARE(m_controller->help(u"/time set "_s).value(u"next"_s).toString(), u"day|night"_s);
    }

    // The words of a structure come from the command's names (decision E-911).
    void structuresCompleteFromTheServersNames()
    {
        QVERIFY(m_admin->receive(catalogue()));
        QCOMPARE(m_controller->complete(u"/locate "_s),
                 (QStringList{u"/locate desert_temple"_s, u"/locate dungeon"_s, u"/locate mineshaft"_s, u"/locate village"_s}));
        QCOMPARE(m_controller->complete(u"/locate mi"_s), QStringList{u"/locate mineshaft"_s});
        QCOMPARE(m_controller->help(u"/locate "_s).value(u"next"_s).toString(), u"<structure>"_s);
    }

    // The command list (decision E-911): by group, then name; the filter searches names, aliases,
    // texts and groups.
    void theCommandListIsGroupedAndSearchable()
    {
        QcAdminCommandList *list = m_controller->commands();
        QCOMPARE(list->count(), 0);
        QVERIFY(m_admin->receive(catalogue()));
        QCOMPARE(list->total(), 5);
        QStringList order;
        for (const QcAdminCommandList::Entry &entry : list->shown()) {
            order.append(entry.name);
        }
        QCOMPARE(order, (QStringList{u"help"_s, u"tp"_s, u"time"_s, u"locate"_s, u"warp"_s}));
        QCOMPARE(list->data(list->index(0), QcAdminCommandList::GroupRole).toString(), u"General"_s);
        QCOMPARE(list->data(list->index(3), QcAdminCommandList::DescriptionRole).toString(), u"Finds the nearest structure."_s);
        QCOMPARE(list->data(list->index(4), QcAdminCommandList::GroupRole).toString(), u"More commands"_s);
        QCOMPARE(list->data(list->index(4), QcAdminCommandList::OperatorRole).toBool(), false);

        QSignalSpy counted(list, &QcAdminCommandList::countChanged);
        list->setFilter(u"TELEPORT"_s);
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->shown().first().name, u"tp"_s);
        QCOMPARE(counted.size(), 1);
        list->setFilter(u"/loc"_s);
        QCOMPARE(list->shown().first().name, u"locate"_s);
        list->setFilter(u"finding"_s);
        QCOMPARE(list->count(), 1);
        list->setFilter(u"nothing like this"_s);
        QCOMPARE(list->count(), 0);
        // The same filter again changes nothing.
        list->setFilter(u"nothing like this"_s);
        QCOMPARE(counted.size(), 4);
        list->setFilter({});
        QCOMPARE(list->count(), 5);
    }

    void reportsBecomeValues()
    {
        m_controller->open();
        m_controller->setChartsShown(true);
        QcMsg::AdminHistory history;
        history.seconds = {sample(1000, 19.0f), sample(2000, 20.0f)};
        history.minutes = {sample(60000, 18.5f)};
        QVERIFY(m_admin->receive(history));
        QcMsg::AdminStatus status;
        status.sample = sample(3000, 19.5f);
        status.phaseMeanMs[0] = 0.5f;
        status.phaseMeanMs[3] = 2.0f;
        status.worldName = u"Welt"_s;
        QVERIFY(m_admin->receive(status));

        QCOMPARE(m_controller->status().value(u"tps"_s).toFloat(), 19.5f);
        QCOMPARE(m_controller->status().value(u"world"_s).toString(), u"Welt"_s);
        const QVariantList phases = m_controller->status().value(u"phases"_s).toList();
        QCOMPARE(phases.size(), 5);
        QCOMPARE(phases[0].toMap().value(u"name"_s).toString(), QString::fromLatin1(QcTickPhases::ids[3]));
        // Each status adds its sample to the chart.
        QCOMPARE(m_controller->charts().value(u"tps"_s).toList().size(), 3);
        m_controller->setMinutes(true);
        QCOMPARE(m_controller->charts().value(u"tps"_s).toList(), QVariantList{18.5});

        QcMsg::AdminPlayers players;
        players.players.append({7, u"zoe"_s, 40, {}, QcGameMode::Creative, false, false, 0, u"10.0.0.2"_s});
        players.players.append({8, u"Anna"_s, -1, {}, QcGameMode::Survival, true, true, 0, {}});
        QVERIFY(m_admin->receive(players));
        QCOMPARE(m_controller->players().size(), 2);
        QCOMPARE(m_controller->players()[0].toMap().value(u"name"_s).toString(), u"Anna"_s);
        QCOMPARE(m_controller->players()[1].toMap().value(u"gameMode"_s).toInt(), int(QcGameMode::Creative));
        QCOMPARE(m_controller->players()[1].toMap().value(u"modeName"_s).toString(), u"Creative"_s);

        QcMsg::AdminLists lists;
        lists.whitelistEnabled = true;
        lists.operators = {u"zoe"_s, u"Anna"_s};
        lists.bans.append({u"Mallory"_s, u"griefing"_s, u"Anna"_s, 0});
        QVERIFY(m_admin->receive(lists));
        QVERIFY(m_controller->lists().value(u"whitelistEnabled"_s).toBool());
        QCOMPARE(m_controller->lists().value(u"operators"_s).toStringList(), (QStringList{u"Anna"_s, u"zoe"_s}));
        QCOMPARE(m_controller->lists().value(u"bans"_s).toList()[0].toMap().value(u"reason"_s).toString(), u"griefing"_s);
    }

    void settingsComeInTheirGroups()
    {
        QcMsg::AdminSettings settings;
        using Setting = QcMsg::AdminSettings::Setting;
        using Type = QcMsg::AdminSettings::Type;
        settings.rules.append(Setting{u"keepInventory"_s, u"Players"_s, u"Keep it."_s, Type::Boolean, 0, 1, 0, 1});
        settings.rules.append(Setting{u"daylightCycle"_s, u"World"_s, u"Time runs."_s, Type::Boolean, 0, 1, 1, 1});
        settings.rules.append(Setting{u"spawnRadius"_s, u"Players"_s, u"Radius."_s, Type::Integer, 0, 64, 10, 12});
        settings.limits.append(Setting{u"maxEntities"_s, u"Creatures"_s, u"Most."_s, Type::Integer, 1, 5000, 300, 300});
        settings.limitsFromServer = true;
        QVERIFY(m_admin->receive(settings));

        const QVariantList rules = m_controller->ruleGroups();
        QCOMPARE(rules.size(), 2);
        QCOMPARE(rules[0].toMap().value(u"title"_s).toString(), u"Players"_s);
        const QVariantList players = rules[0].toMap().value(u"settings"_s).toList();
        QCOMPARE(players.size(), 2);
        QCOMPARE(players[1].toMap().value(u"key"_s).toString(), u"spawnRadius"_s);
        QCOMPARE(players[1].toMap().value(u"high"_s).toLongLong(), 64);
        // Known keys have a name (the key stays beside it), others show as their key.
        QCOMPARE(players[0].toMap().value(u"name"_s).toString(), u"Keep inventory"_s);
        QVERIFY(players[0].toMap().value(u"named"_s).toBool());
        QCOMPARE(players[1].toMap().value(u"name"_s).toString(), u"spawnRadius"_s);
        QVERIFY(!players[1].toMap().value(u"named"_s).toBool());
        QCOMPARE(players[1].toMap().value(u"defaultText"_s).toString(), QLocale().toString(10));
        QVERIFY(!players[1].toMap().value(u"limit"_s).toBool());
        QVERIFY(players[0].toMap().value(u"boolean"_s).toBool());
        QVERIFY(m_controller->limitGroups()[0].toMap().value(u"settings"_s).toList()[0].toMap().value(u"limit"_s).toBool());
        QVERIFY(m_controller->limitsFromServer());
    }

    void theLogLevelFollowsItsTail()
    {
        m_controller->setLogLevel(QcAdminController::LogLevel::Warning);
        // Closed: nothing goes out yet; opening follows at that level.
        QVERIFY(sent<QcMsg::AdminRequest>().isEmpty());
        m_controller->open();
        QCOMPARE(sent<QcMsg::AdminSubscribe>().constLast().logLevel, quint8(QcMsg::AdminLogLine::Level::Warning));

        QVERIFY(m_admin->receive(QcMsg::AdminLog{true, {{1, QcMsg::AdminLogLine::Level::Warning, u"server"_s, u"old"_s}}}));
        QCOMPARE(m_controller->log()->count(), 1);

        // A new level follows anew (the server answers with its tail); no LogTail, whose rate
        // limit two quick changes would hit, and the lines stay until the tail replaces them.
        const qsizetype requests = sent<QcMsg::AdminRequest>().size();
        m_controller->setLogLevel(QcAdminController::LogLevel::Debug);
        m_controller->setLogLevel(QcAdminController::LogLevel::Info);
        m_controller->setLogLevel(QcAdminController::LogLevel::Debug);
        QCOMPARE(sent<QcMsg::AdminSubscribe>().constLast().logLevel, quint8(QcMsg::AdminLogLine::Level::Debug));
        QCOMPARE(sent<QcMsg::AdminRequest>().size(), requests);
        QCOMPARE(m_controller->log()->count(), 1);
        QVERIFY(m_admin->receive(QcMsg::AdminLog{true, {{2, QcMsg::AdminLogLine::Level::Debug, u"server"_s, u"a"_s},
                                                        {3, QcMsg::AdminLogLine::Level::Info, u"server"_s, u"b"_s}}}));
        QCOMPARE(m_controller->log()->count(), 2);
        QCOMPARE(m_controller->log()->lines().constFirst().text, u"a"_s);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"no log level 9"_s));
        m_controller->setLogLevel(static_cast<QcAdminController::LogLevel>(9));
        QCOMPARE(m_controller->logLevel(), QcAdminController::LogLevel::Debug);
    }

    void linesArriveWithoutAReset()
    {
        m_controller->open();
        QcAdminLineModel *chat = m_controller->chat();
        QSignalSpy resets(chat, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(chat, &QAbstractItemModel::rowsInserted);
        QcMsg::AdminChat history{true, {{1000, u"Anna"_s, plain(u"hi"_s)}}};
        QVERIFY(m_admin->receive(history));
        QCOMPARE(chat->count(), 1);
        QCOMPARE(resets.size(), 1);

        // The same line twice in one millisecond still counts twice.
        QVERIFY(m_admin->receive(QcMsg::AdminChat{false, {{1000, u"Anna"_s, plain(u"hi"_s)}}}));
        QVERIFY(m_admin->receive(QcMsg::AdminChat{false, {{2000, {}, plain(u"[Server] welcome"_s)}}}));
        QCOMPARE(chat->count(), 3);
        QCOMPARE(resets.size(), 1);
        QCOMPARE(inserts.size(), 2);
        QCOMPARE(chat->data(chat->index(2), QcAdminLineModel::SourceRole).toString(), QString());
        QCOMPARE(chat->data(chat->index(0), QcAdminLineModel::TextRole).toString(), u"hi"_s);
    }

    void theOldestLinesGo()
    {
        m_controller->open();
        QcAdminLineModel *log = m_controller->log();
        QList<QcMsg::AdminLogLine> tail;
        for (int i = 0; i < static_cast<int>(QcClientAdmin::logLimit); ++i) {
            tail.append({i, QcMsg::AdminLogLine::Level::Info, u"qtcraft.server"_s, u"line %1"_s.arg(i)});
        }
        QVERIFY(m_admin->receive(QcMsg::AdminLog{true, tail}));
        QCOMPARE(log->count(), int(QcClientAdmin::logLimit));
        QSignalSpy resets(log, &QAbstractItemModel::modelReset);
        QSignalSpy removes(log, &QAbstractItemModel::rowsRemoved);
        QVERIFY(m_admin->receive(QcMsg::AdminLog{false, {{9999, QcMsg::AdminLogLine::Level::Warning, u"qtcraft.lag"_s, u"slow"_s}}}));
        QCOMPARE(log->count(), int(QcClientAdmin::logLimit));
        QCOMPARE(log->lines().last().text, u"slow"_s);
        QCOMPARE(log->lines().first().text, u"line 1"_s);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(removes.size(), 1);

        // A new tail replaces them all.
        QVERIFY(m_admin->receive(QcMsg::AdminLog{true, {{1, QcMsg::AdminLogLine::Level::Info, {}, u"new"_s}}}));
        QCOMPARE(resets.size(), 1);
        QCOMPARE(log->count(), 1);
    }

    // The draft lives in the controller: it outlives the screen closing, takes 64-bit values
    // without clamping, and saving keeps what the server refused, with one notice for all.
    void theDraftOutlivesClosingAndKeepsRefusals()
    {
        using Setting = QcMsg::AdminSettings::Setting;
        using Type = QcMsg::AdminSettings::Type;
        QcMsg::AdminSettings settings;
        settings.rules.append(Setting{u"keepInventory"_s, u"Players"_s, u"Keep it."_s, Type::Boolean, 0, 1, 0, 0});
        settings.limits.append(Setting{u"signalBudget"_s, u"Signal"_s, u"Work."_s, Type::Integer, 256, qint64{1} << 40, 1000, 1000});
        settings.limits.append(Setting{u"fires"_s, u"Fire"_s, u"Fires."_s, Type::Integer, 16, 4096, 100, 100});
        m_controller->open();
        QVERIFY(m_admin->receive(settings));

        m_controller->changeSetting(false, u"keepInventory"_s, 1);
        QVERIFY(m_controller->changeSettingText(true, u"signalBudget"_s, u"5000000000"_s));
        QVERIFY(!m_controller->changeSettingText(true, u"fires"_s, u"99999"_s));
        QVERIFY(!m_controller->changeSettingText(true, u"fires"_s, u"-"_s));
        QVERIFY(m_controller->changeSettingText(true, u"fires"_s, u"200"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"out of the range of \"?fires"_s));
        m_controller->changeSetting(true, u"fires"_s, 5);
        QCOMPARE(m_controller->draft().value(u"limit:signalBudget"_s).toLongLong(), 5000000000LL);
        QCOMPARE(m_controller->draft().size(), 3);
        // Back to the server's value: out of the draft.
        QVERIFY(m_controller->changeSettingText(true, u"fires"_s, u"100"_s));
        QVERIFY(m_controller->changeSettingText(true, u"fires"_s, u"300"_s));

        // The pause menu closed for another reason: the draft waits for the next open.
        m_controller->close();
        m_controller->open();
        QVERIFY(m_controller->dirty());
        QCOMPARE(m_controller->draft().size(), 3);

        const qsizetype before = sent<QcMsg::AdminRequest>().size();
        m_controller->saveSettings();
        const QList<QcMsg::AdminRequest> requests = sent<QcMsg::AdminRequest>().mid(before);
        QCOMPARE(requests.size(), 3);
        for (const QcMsg::AdminRequest &request : requests) {
            const bool refuse = request.arguments.value(0) == u"fires"_s;
            QcMsg::AdminResult result{request.id, !refuse, {}};
            result.lines.append(plain(refuse ? u"Too many"_s : u"Set"_s));
            QVERIFY(m_admin->receive(result));
            // One notice for all, after the last answer.
            QCOMPARE(m_controller->noticeKind() == QcAdminController::Notice::None, request.id != requests.constLast().id);
        }
        QCOMPARE(m_controller->draft().keys(), QStringList{u"limit:fires"_s});
        QCOMPARE(m_controller->noticeKind(), QcAdminController::Notice::Refused);
        QVERIFY(m_controller->notice().contains(u"fires"_s));
        QVERIFY(m_controller->notice().contains(u"Too many"_s));

        // The server's new values: what it has leaves the draft; a new session drops it.
        settings.limits[1].value = 300;
        QVERIFY(m_admin->receive(settings));
        QVERIFY(!m_controller->dirty());
        m_controller->changeSetting(false, u"keepInventory"_s, 1);
        m_controller->follow(m_admin.get(), {});
        QVERIFY(!m_controller->dirty());
    }

    void rightsGivenBackFollowAgain()
    {
        m_controller->open();
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, true, {}}));
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, false, {}, QcMsg::AdminResult::Refusal::Rights}));
        QCOMPARE(m_controller->noticeKind(), QcAdminController::Notice::Refused);
        const qsizetype subscriptions = sent<QcMsg::AdminSubscribe>().size();
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, true, {}}));
        QCOMPARE(sent<QcMsg::AdminSubscribe>().size(), subscriptions + 1);
        QCOMPARE(sent<QcMsg::AdminSubscribe>().constLast().topics, QcMsg::AdminSubscribe::all);
        QCOMPARE(m_controller->noticeKind(), QcAdminController::Notice::None);
        // Closed, rights given back follow nothing.
        m_controller->close();
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, false, {}, QcMsg::AdminResult::Refusal::Rights}));
        QVERIFY(m_admin->receive(QcMsg::AdminResult{0, true, {}}));
        QCOMPARE(sent<QcMsg::AdminSubscribe>().constLast().topics, quint8(0));
    }

    // Charts are built only while shown, the day's chart only when a minute ends; the client folds
    // every 60 seconds into a minute as the server does.
    void chartsFollowTheDayOnlyWhenShown()
    {
        m_controller->open();
        QSignalSpy charts(m_controller.get(), &QcAdminController::chartsChanged);
        const auto status = [this](qint64 second) {
            QcMsg::AdminStatus report;
            report.sample = sample(second * 1000, 20.0f);
            QVERIFY(m_admin->receive(report));
        };
        status(1);
        QCOMPARE(charts.size(), 0);
        m_controller->setChartsShown(true);
        QCOMPARE(charts.size(), 1);
        status(2);
        QCOMPARE(charts.size(), 2);

        m_controller->setMinutes(true);
        QCOMPARE(m_controller->chartCapacity(), 1440);
        const qsizetype built = charts.size();
        for (qint64 second = 3; second < 60; ++second) {
            status(second);
        }
        QCOMPARE(charts.size(), built);
        QVERIFY(m_controller->charts().value(u"tps"_s).toList().isEmpty());
        status(60);
        QCOMPARE(charts.size(), built + 1);
        QCOMPARE(m_controller->charts().value(u"tps"_s).toList().size(), 1);
        QCOMPARE(m_admin->history().minutes.constLast().timeMs, 60000);
    }

    void theCommandTableRevisesHelp()
    {
        QSignalSpy revised(m_controller.get(), &QcAdminController::catalogueChanged);
        QVERIFY(m_controller->help(u"/time"_s).isEmpty());
        QVERIFY(m_admin->receive(catalogue()));
        QCOMPARE(revised.size(), 1);
        QCOMPARE(m_controller->catalogueRevision(), 1);
        QVERIFY(!m_controller->help(u"/time"_s).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAdminController)
#include "test_admin_controller.moc"
