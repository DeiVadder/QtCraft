#include "common/content/parser/QcCommandMacroParser.h"
#include "QcServerTestFixture.h"
#include "server/commands/QcCommandRegistry.h"
#include "server/commands/QcPendingCommands.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

// Lines of the category qtcraft.commands while a test runs.
QStringList *g_commandLog = nullptr;
QtMessageHandler g_previousHandler = nullptr;

void captureCommands(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (g_commandLog && context.category && QByteArrayView(context.category) == "qtcraft.commands") {
        g_commandLog->append(message);
        return;
    }
    g_previousHandler(type, context, message);
}

} // namespace

// The command layer (decisions E-243, E-245, E-248): rights, logging, help, confirmation, limits
// and macros; player management, maintenance and /tick have their own test programs.
class TestServerCommands : public QcServerTestFixture
{
    Q_OBJECT

private:
    QStringList m_log;

    QString lastLine() const { return m_client->chatLines.isEmpty() ? QString() : m_client->chatLines.constLast(); }

    QStringList console(const QString &line)
    {
        QStringList lines;
        m_server->runConsoleCommand(line, [&lines](const QcMsg::ChatMessage &reply) { lines.append(reply.text); });
        pump();
        return lines;
    }

    QcBlockPos east(int dx) const { return m_feet.offset(dx, 0, 0); }

private slots:
    void init()
    {
        m_log.clear();
        g_commandLog = &m_log;
        g_previousHandler = qInstallMessageHandler(captureCommands);
    }

    void cleanup()
    {
        qInstallMessageHandler(g_previousHandler);
        g_commandLog = nullptr;
        m_client.reset();
        m_server.reset();
        m_origin = QcServer::ClientOrigin::Remote;
        m_content = loadContent();
    }

    void playersMayUseOnlyPlayerCommands()
    {
        join(notOperator());
        chat(u"/give dirt"_s);
        QCOMPARE(lastLine(), u"Only operators can use /give"_s);
        QCOMPARE(m_client->chatKeys.constLast().key, u"Only operators can use /%1"_s);
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        chat(u"/fill ~1 ~ ~ ~1 ~ ~ glass"_s);
        QCOMPARE(*m_server->world().blockAt(east(1)), qcAirBlockId);

        // Player commands work for everyone.
        chat(u"/list"_s);
        QCOMPARE(lastLine(), u"1 players online: Raw"_s);
        chat(u"/gamerule keepInventory"_s);
        QCOMPARE(lastLine(), u"keepInventory is true"_s);
        // The value is part of the translated line, not an untranslated argument.
        QCOMPARE(m_client->chatKeys.constLast().key, u"%1 is true"_s);
        QCOMPARE(m_client->chatKeys.constLast().arguments, QStringList{u"keepInventory"_s});

        // /help lists only what the player may use.
        chat(u"/help"_s);
        const QString help = m_client->chatLines.at(m_client->chatLines.size() - 2);
        QVERIFY(help.contains(u"/list"_s) && help.contains(u"/help"_s) && help.contains(u"/confirm"_s));
        QVERIFY(!help.contains(u"/give"_s) && !help.contains(u"/ban"_s) && !help.contains(u"/stop"_s));
    }

    void theLocalPlayerIsAlwaysAnOperator()
    {
        m_origin = QcServer::ClientOrigin::Local;
        join(notOperator());
        chat(u"/give dirt 3"_s);
        QCOMPARE(countOf(item("qtcraft:dirt")), 3);

        // He cannot lose the rights, be kicked or banned: he hosts the world.
        chat(u"/deop Raw"_s);
        QCOMPARE(lastLine(), u"The host of the world is always an operator"_s);
        chat(u"/kick Raw"_s);
        QCOMPARE(lastLine(), u"The host of the world cannot be kicked or banned"_s);
        chat(u"/ban Raw"_s);
        QCOMPARE(lastLine(), u"The host of the world cannot be kicked or banned"_s);
        QVERIFY(m_client->disconnectReason.isEmpty());
        QVERIFY(m_server->world().blockAt(m_feet).has_value());
    }

    void operatorsAreGrantedAndRevoked()
    {
        join(notOperator());
        console(u"op raw"_s);
        QCOMPARE(lastLine(), u"You are now an operator"_s);
        chat(u"/give dirt"_s);
        QCOMPARE(countOf(item("qtcraft:dirt")), 1);

        console(u"/deop RAW"_s);
        QCOMPARE(lastLine(), u"You are no longer an operator"_s);
        chat(u"/give dirt"_s);
        QCOMPARE(countOf(item("qtcraft:dirt")), 1);
    }

    void theConsoleHasAllRightsButNoPosition()
    {
        join(notOperator());
        QCOMPARE(console(u"time set 100"_s), QStringList{u"Time is now 100 s (day)"_s});
        QCOMPARE(m_server->worldTime(), 100.0);
        QCOMPARE(console(u"give dirt"_s), QStringList{u"Only players can use /give"_s});
        // Relative coordinates need a position.
        QVERIFY(console(u"setblock ~ ~ ~ glass"_s).constFirst().startsWith(u"Usage"_s));
        QCOMPARE(console(u"setblock %1 %2 %3 glass"_s.arg(m_feet.x + 1).arg(m_feet.y).arg(m_feet.z)).size(), 1);
        QCOMPARE(*m_server->world().blockAt(east(1)), id("qtcraft:glass"));
        QCOMPARE(console(u"nonsense"_s), QStringList{u"Unknown command: /nonsense"_s});

        // Operators may not stop the server; only the console.
        QSignalSpy stop(m_server.get(), &QcServer::stopRequested);
        console(u"op Raw"_s);
        chat(u"/stop"_s);
        QCOMPARE(lastLine(), u"Only the server console can use /stop"_s);
        QCOMPARE(stop.count(), 0);
        QCOMPARE(console(u"stop"_s), QStringList{u"Stopping the server"_s});
        QCOMPARE(stop.count(), 1);
    }

    void everyCommandIsLoggedWithItsSource()
    {
        join(notOperator());
        chat(u"/give dirt 2"_s);
        console(u"list"_s);
        chat(u"hello everyone"_s);
        QVERIFY(m_log.contains(u"Raw (player): /give dirt 2"_s));
        QVERIFY(m_log.contains(u"Raw may not use /give"_s));
        QVERIFY(m_log.contains(u"Server (console): /list"_s));
        // Plain chat is no command.
        QVERIFY(std::ranges::none_of(m_log, [](const QString &line) { return line.contains(u"hello"_s); }));
    }

    void helpShowsTheUsageOfACommand()
    {
        join();
        chat(u"/help give"_s);
        QCOMPARE(m_client->chatLines.at(m_client->chatLines.size() - 2), u"Usage: /give <item> [count]"_s);
        QCOMPARE(lastLine(), u"Puts items into your inventory."_s);
        // Aliases find the command, too short calls answer with the usage.
        chat(u"/help /pardon"_s);
        QCOMPARE(m_client->chatLines.at(m_client->chatLines.size() - 2), u"Usage: /unban <player>"_s);
        chat(u"/tp"_s);
        QVERIFY(lastLine().startsWith(u"Usage: /tp"_s));
        chat(u"/help nothing"_s);
        QCOMPARE(lastLine(), u"Unknown command: nothing"_s);
    }

    void everyCommandHasUsageAndDescription()
    {
        join();
        const QList<const QcCommandSpec *> commands = m_server->commands().registry().commands();
        QVERIFY(commands.size() >= 30);
        for (const QcCommandSpec *spec : commands) {
            QVERIFY2(QString::fromUtf8(spec->usage).startsWith(u"Usage: /"_s + spec->name), spec->usage);
            QVERIFY(qstrlen(spec->description) >= 8);
        }
    }

    void theRegistryRefusesTakenAndInvalidNames()
    {
        QcCommandRegistry registry;
        const auto spec = [](const QString &name, QStringList aliases = {}) {
            return QcCommandSpec{.name = name, .aliases = std::move(aliases), .usage = "Usage: /x", .description = "Test command.",
                                 .run = [](const QcCommandContext &) {}};
        };
        QVERIFY(registry.add(spec(u"warp"_s, {u"w"_s})).has_value());
        QVERIFY(!registry.add(spec(u"warp"_s)).has_value());
        QVERIFY(!registry.add(spec(u"other"_s, {u"w"_s})).has_value());
        QVERIFY(!registry.add(spec(u"Warp"_s)).has_value());
        QVERIFY(!registry.add(spec(u"a b"_s)).has_value());
        QVERIFY(!registry.add(spec(QString(33, u'a'))).has_value());
        QVERIFY(!registry.add(QcCommandSpec{.name = u"nohandler"_s, .usage = "u", .description = "d"}).has_value());
        QCOMPARE(registry.find(u"w")->name, u"warp"_s);
        QCOMPARE(registry.commands().size(), 1);
    }

    void cppExtensionsAddCommands()
    {
        join();
        int calls = 0;
        QVERIFY(m_server->commands().add({.name = u"ping"_s, .permission = QcPermission::Player,
                                          .usage = "Usage: /ping", .description = "Answers pong.",
                                          .run = [&calls](const QcCommandContext &context) {
                                              ++calls;
                                              context.source.replyPlain(u"pong "_s + context.args.join(u','));
                                          }})
                    .has_value());
        QVERIFY(!m_server->commands().add({.name = u"give"_s, .usage = "u", .description = "d", .run = [](const QcCommandContext &) {}})
                     .has_value());
        chat(u"/ping a b"_s);
        QCOMPARE(lastLine(), u"pong a,b"_s);
        QCOMPARE(calls, 1);
    }

    void largeFillsWaitForConfirmation()
    {
        join();
        // 4096 blocks run at once, more ask first.
        chat(u"/fill ~1 ~ ~ ~16 ~ ~255 glass"_s);
        QCOMPARE(*m_server->world().blockAt(east(1)), id("qtcraft:glass"));
        chat(u"/fill ~-2 ~ ~-2 ~-2 ~ ~-2 air"_s);

        chat(u"/fill ~1 ~1 ~ ~17 ~1 ~255 stone"_s);
        QCOMPARE(lastLine(), u"Type /confirm within 30 s to go ahead or /cancel to drop it"_s);
        QCOMPARE(*m_server->world().blockAt(east(1).offset(0, 1, 0)), qcAirBlockId);
        chat(u"/cancel"_s);
        QCOMPARE(lastLine(), u"Cancelled"_s);
        chat(u"/confirm"_s);
        QCOMPARE(lastLine(), u"Nothing to confirm"_s);

        chat(u"/fill ~1 ~1 ~ ~17 ~1 ~255 stone"_s);
        chat(u"/confirm"_s);
        QVERIFY(lastLine().startsWith(u"Filled"_s));
        QCOMPARE(*m_server->world().blockAt(east(1).offset(0, 1, 0)), id("qtcraft:stone"));
    }

    void confirmationsExpireAndBelongToTheirSource()
    {
        join();
        chat(u"/regenerate"_s);
        // The console has no pending action of its own.
        QCOMPARE(console(u"confirm"_s), QStringList{u"Nothing to confirm"_s});
        ticks(31 * 20);
        chat(u"/confirm"_s);
        QCOMPARE(lastLine(), u"Too late: confirm within 30 s, run the command again"_s);
        QCOMPARE(m_server->world().blockAt(m_feet.offset(0, -1, 0)), std::optional<QcBlockId>(id("qtcraft:grass_block")));
    }

    void confirmationsNeedTheRightsAtConfirmTime()
    {
        // A destructive command waiting for /confirm does not outlive the rights it needs: /deop
        // drops it.
        join();
        chat(u"/regenerate"_s);
        QCOMPARE(lastLine(), u"Type /confirm within 30 s to go ahead or /cancel to drop it"_s);
        QCOMPARE(console(u"deop Raw"_s), QStringList{u"Raw is no operator"_s});
        chat(u"/confirm"_s);
        QCOMPARE(lastLine(), u"Nothing to confirm"_s);
        console(u"op Raw"_s);
        chat(u"/confirm"_s);
        QCOMPARE(lastLine(), u"Nothing to confirm"_s);

        // Should one still be waiting, the confirmation checks the source's rights again.
        QcServerClient client;
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        client.connection = std::make_unique<QcConnection>(std::move(serverEnd), QcProtocol::Peer::Client, [](QcMessage &&) {});
        client.id = 77;
        QcPendingCommands pending;
        bool ran = false;
        const QStringList args;
        const QString text;
        const QcCommandSource asOperator(client, QcPermission::Operator);
        pending.ask({asOperator, args, text, QcPermission::Operator}, [&ran](const QcCommandSource &) { ran = true; }, 0);
        QVERIFY(!pending.confirm(QcCommandSource(client, QcPermission::Player), 1));
        QVERIFY(!ran);
        QVERIFY(!pending.hasPending(77));
        pending.ask({asOperator, args, text, QcPermission::Operator}, [&ran](const QcCommandSource &) { ran = true; }, 0);
        QVERIFY(pending.confirm(asOperator, QcPendingCommands::timeoutNs));
        QVERIFY(ran);
    }

    void confirmationsExpireInRealTime()
    {
        // 30 s of real time: at 1 tick per second that is 30 ticks, not 600.
        join();
        chat(u"/tick rate 1"_s);
        chat(u"/regenerate"_s);
        ticks(29);
        chat(u"/cancel"_s);
        QCOMPARE(lastLine(), u"Cancelled"_s);
        chat(u"/regenerate"_s);
        ticks(31);
        chat(u"/confirm"_s);
        QCOMPARE(lastLine(), u"Too late: confirm within 30 s, run the command again"_s);
    }

    void fillHasLimitsAndAReplaceMode()
    {
        join();
        // Larger than the limit, outside the world height, unknown blocks and words.
        chat(u"/fill ~ ~ ~ ~40 ~40 ~40 stone"_s);
        QVERIFY(lastLine().startsWith(u"Usage: /fill"_s));
        chat(u"/fill ~ 250 ~ ~ 260 ~ stone"_s);
        QVERIFY(lastLine().startsWith(u"Usage: /fill"_s));
        chat(u"/fill ~1 ~ ~ ~1 ~ ~ nothing"_s);
        QCOMPARE(lastLine(), u"Unknown block: nothing"_s);
        chat(u"/fill ~1 ~ ~ ~1 ~ ~ stone keep"_s);
        QVERIFY(lastLine().startsWith(u"Usage: /fill"_s));

        // Only grass turns into stone; air above stays air.
        chat(u"/fill ~-3 ~-1 ~-3 ~3 ~ ~3 stone replace grass_block"_s);
        QCOMPARE(lastLine(), u"Filled 49 blocks with qtcraft:stone"_s);
        QCOMPARE(*m_server->world().blockAt(m_feet.offset(2, -1, 2)), id("qtcraft:stone"));
        QCOMPARE(*m_server->world().blockAt(east(2)), qcAirBlockId);

        // /clear with a filter takes only that block.
        QVERIFY(m_server->world().setBlock(east(1), id("qtcraft:glass")));
        QVERIFY(m_server->world().setBlock(east(2), id("qtcraft:dirt")));
        chat(u"/clear ~1 ~ ~ ~2 ~ ~ glass"_s);
        QCOMPARE(*m_server->world().blockAt(east(1)), qcAirBlockId);
        QCOMPARE(*m_server->world().blockAt(east(2)), id("qtcraft:dirt"));
        chat(u"/clear ~1 ~ ~ ~2 ~ ~"_s);
        QCOMPARE(*m_server->world().blockAt(east(2)), qcAirBlockId);
    }

    void extremeCoordinatesAreRefused()
    {
        // Numbers at the ends of 32 bits, or beyond them, are refused before anything is computed
        // with them: no confirmation, no loop over billions of blocks.
        join();
        const QStringList fills{u"/fill -2147483648 60 0 2147483647 60 0 stone"_s, u"/fill 2147483647 60 0 2147483647 60 0 stone"_s,
                                u"/fill -2147483648 60 -2147483648 -2147483648 60 -2147483648 stone"_s,
                                u"/fill 0 -2147483648 0 0 2147483647 0 stone"_s, u"/clear 29999999 60 0 -29999999 60 0"_s,
                                u"/fill ~9223372036854775807 ~ ~ ~ ~ ~ stone"_s, u"/fill ~-2147483648 ~ ~ ~2147483647 ~ ~ stone"_s,
                                u"/fill 99999999999 60 0 0 60 0 stone"_s};
        for (const QString &fill : fills) {
            chat(fill);
            QVERIFY2(lastLine().startsWith(u"Usage: /"_s), qPrintable(fill + u" -> "_s + lastLine()));
        }
        chat(u"/setblock 2147483647 60 2147483647 stone"_s);
        QCOMPARE(lastLine(), u"Usage: /setblock <x> <y> <z> <block>[states] [creature]"_s);
        chat(u"/setblock 0 2147483647 0 stone"_s);
        QCOMPARE(lastLine(), u"Usage: /setblock <x> <y> <z> <block>[states] [creature]"_s);
        for (const QString &regenerate : {u"/regenerate -2147483648 0 2147483647 0"_s, u"/regenerate 2147483647 2147483647"_s,
                                          u"/regenerate ~-2147483648 ~"_s}) {
            chat(regenerate);
            QVERIFY2(lastLine().startsWith(u"Usage: /regenerate"_s), qPrintable(regenerate + u" -> "_s + lastLine()));
        }
        for (const QString &teleport : {u"/tp 2147483647 70 0"_s, u"/tp -2147483648 70 -2147483648"_s, u"/tp 0 70 1e300"_s}) {
            chat(teleport);
            QVERIFY2(lastLine().startsWith(u"Usage: /tp"_s), qPrintable(teleport));
        }
        chat(u"/confirm"_s);
        QCOMPARE(lastLine(), u"Nothing to confirm"_s);

        // The edge of the world limit itself still works (a chunk there is not loaded).
        chat(u"/setblock 30000000 60 -30000000 stone"_s);
        QCOMPARE(lastLine(), u"Nothing set: the chunks are not loaded"_s);
    }

    void teleportMovesPlayers()
    {
        join();
        chat(u"/tp 100.5 80 -40.5"_s);
        ticks(1);
        QCOMPARE(m_client->motion.position.x, 100.5);
        QCOMPARE(m_client->motion.position.z, -40.5);
        QVERIFY(m_server->world().chunk(QcBlockPos{100, 80, -41}.chunk()) != nullptr);

        chat(u"/tp ~1 ~ ~"_s);
        ticks(1);
        QCOMPARE(m_client->motion.position.x, 101.5);
        chat(u"/tp Raw 0 70 0"_s);
        ticks(1);
        QCOMPARE(m_client->motion.position.x, 0.0);

        // Invalid numbers, positions outside the world and unknown players change nothing.
        for (const QString &bad : {u"/tp 1 nan 1"_s, u"/tp 1 900 1"_s, u"/tp 99999999 70 0"_s, u"/tp Nobody"_s, u"/tp 1 2"_s}) {
            chat(bad);
            QVERIFY2(lastLine().startsWith(u"Usage: /tp"_s), qPrintable(bad));
        }
        ticks(1);
        QCOMPARE(m_client->motion.position.x, 0.0);
    }

    void gameModeAndTimeQueries()
    {
        join();
        chat(u"/gamemode"_s);
        QCOMPARE(lastLine(), u"Game mode of Raw: survival"_s);
        chat(u"/gamemode creative"_s);
        QCOMPARE(lastLine(), u"Set the game mode of Raw to creative"_s);
        // The mode is part of the key, so the client translates it with the line (E-094).
        QCOMPARE(m_client->chatKeys.constLast().key, u"Set the game mode of %1 to creative"_s);
        chat(u"/gamemode"_s);
        QCOMPARE(lastLine(), u"Game mode of Raw: creative"_s);
        QCOMPARE(m_client->chatKeys.constLast().key, u"Game mode of %1: creative"_s);
        chat(u"/gamemode adventure"_s);
        QCOMPARE(lastLine(), u"Unknown game mode adventure; use survival or creative"_s);
        chat(u"/gamemode survival Nobody"_s);
        QCOMPARE(lastLine(), u"No player named Nobody is online"_s);
        chat(u"/time"_s);
        QVERIFY(lastLine().startsWith(u"Time is now"_s));
    }

    void everyoneMayAskForTheirOwnGameMode()
    {
        join(notOperator());
        chat(u"/gamemode"_s);
        QCOMPARE(lastLine(), u"Game mode of Raw: survival"_s);
        chat(u"/gamemode creative"_s);
        QCOMPARE(m_client->chatKeys.constLast().key, u"Only operators can use /%1"_s);
        QCOMPARE(m_client->gameMode, QcGameMode::Survival);
    }

    void macrosRunTheirLinesWithTheCallersRights()
    {
        auto content = std::make_shared<QcContentRegistry>(*m_content);
        const auto macro = [](const char *json) {
            return QcCommandMacroParser::parseCommandMacro(QJsonDocument::fromJson(json).object(), u"testmod"_s);
        };
        content->registerCommandMacro(*macro(R"({"name":"kit","permission":"player","description":"Starter kit.",
                                                   "run":["/give $1 $2","/give torch"]})"));
        content->registerCommandMacro(*macro(R"({"name":"nested","permission":"player","run":["/kit stone 1"]})"));
        content->registerCommandMacro(*macro(R"({"name":"give","permission":"player","run":["/list"]})"));
        content->registerCommandMacro(*macro(R"({"name":"wipe","description":"Operators only.","run":["/remove all"]})"));
        m_content = content;

        join(notOperator());
        // Help shows only the macros the caller may run.
        chat(u"/help wipe"_s);
        QCOMPARE(lastLine(), u"Unknown command: wipe"_s);
        chat(u"/help"_s);
        QVERIFY(!m_client->chatLines.at(m_client->chatLines.size() - 2).contains(u"/wipe"_s));
        QVERIFY(m_client->chatLines.at(m_client->chatLines.size() - 2).contains(u"/kit"_s));
        // Every line is checked for the caller: a player cannot give himself items through a macro.
        chat(u"/kit dirt 5"_s);
        QCOMPARE(lastLine(), u"Only operators can use /give"_s);
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        // A macro never replaces a command of the game.
        chat(u"/give dirt"_s);
        QCOMPARE(lastLine(), u"Only operators can use /give"_s);

        console(u"op Raw"_s);
        chat(u"/kit dirt 5"_s);
        QCOMPARE(countOf(item("qtcraft:dirt")), 5);
        QCOMPARE(countOf(item("qtcraft:torch")), 1);
        chat(u"/kit dirt"_s);
        QCOMPARE(lastLine(), u"/kit"_s);
        // Macros cannot call macros.
        chat(u"/nested"_s);
        QCOMPARE(lastLine(), u"Unknown command: /kit"_s);
        chat(u"/help kit"_s);
        QCOMPARE(lastLine(), u"Starter kit."_s);
        chat(u"/help wipe"_s);
        QCOMPARE(lastLine(), u"Operators only."_s);
    }

    void macroDefinitionsAreChecked()
    {
        const auto parse = [](const char *json) {
            return QcCommandMacroParser::parseCommandMacro(QJsonDocument::fromJson(json).object(), u"testmod"_s);
        };
        QVERIFY(parse(R"({"name":"ok","run":["/time set day"]})").has_value());
        QCOMPARE(parse(R"({"name":"ok","run":["/give $3"]})")->requiredArguments, 3);
        QVERIFY(parse(R"({"name":"ok","run":["/give $*"]})")->operatorOnly);
        for (const char *bad : {R"({"name":"Bad","run":["/list"]})", R"({"name":"ok","run":[]})", R"({"name":"ok","run":["list"]})",
                                R"({"name":"ok","run":["/give $x"]})", R"({"name":"ok","permission":"admin","run":["/list"]})",
                                R"({"name":"ok","run":"/list"})"}) {
            QVERIFY2(!parse(bad).has_value(), bad);
        }
        QJsonArray tooMany;
        for (int i = 0; i <= QcCommandMacroDefinition::maxLines; ++i) {
            tooMany.append(u"/list"_s);
        }
        QVERIFY(!QcCommandMacroParser::parseCommandMacro(QJsonObject{{u"name"_s, u"ok"_s}, {u"run"_s, tooMany}}, u"t"_s).has_value());
    }
};

QTEST_GUILESS_MAIN(TestServerCommands)
#include "test_server_commands.moc"
