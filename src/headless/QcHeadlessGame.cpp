#include "headless/QcHeadlessGame.h"

#include "clientcore/control/QcControlInterface.h"
#include "clientcore/debug/QcClientHealth.h"
#include "clientcore/debug/QcDebugJson.h"
#include "clientcore/debug/QcStatsLog.h"
#include "clientcore/session/QcClientSession.h"
#include "common/diagnostics/QcDiagnosticsSession.h"
#include "common/diagnostics/QcHealthLog.h"
#include "common/gameplay/QcKeyBindings.h"
#include "common/handbook/builder/QcHandbookBuilder.h"
#include "common/handbook/QcHandbookJson.h"
#include "common/protocol/QcTransport.h"
#include "headless/QcDedicatedServer.h"
#include "headless/QcHeadlessDiagnostics.h"
#include "headless/QcHeadlessJoin.h"
#include "headless/QcHeadlessOptions.h"
#include "headless/QcHeadlessTools.h"
#include "headless/QcServerConsole.h"
#include "headless/QcSignalWatcher.h"
#include "headless/QcStdinReader.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcContentFingerprint.h"
#include "worlds/QcHandbookRules.h"
#include "worlds/QcLocalServerHost.h"
#include "worlds/QcModLoader.h"
#include "worlds/QcWorldLibrary.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>
#include <print>

using namespace Qt::StringLiterals;

namespace {

using QcHeadlessOptions::Options;
using QcHeadlessOptions::printError;
using QcHeadlessTools::exitSoon;

// The response of a command that failed.
[[nodiscard]] QJsonObject failure(const QString &error)
{
    return {{u"ok"_s, false}, {u"error"_s, error}};
}

} // namespace

QcHeadlessGame::QcHeadlessGame(QObject *parent)
    : QObject(parent)
{
}

QcHeadlessGame::~QcHeadlessGame()
{
    m_dedicated.reset();
    if (m_remote && m_session) {
        // Says goodbye so the server saves the player at once rather than when the link drops.
        m_session->disconnectFromServer();
    }
    // The bot leaves before the server shuts down and reports how the final save went.
    m_control.reset();
    m_session.reset();
    if (m_server) {
        if (const QcStatus stopped = m_server->shutdown(); !stopped) {
            printError(u"saving the world failed: "_s + stopped.error());
        }
    }
}

bool QcHeadlessGame::start(const QStringList &arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"QtCraft headless server with a control-interface client."_s);
    parser.addHelpOption();
    const Options options;
    options.addTo(parser);
    if (!parser.parse(arguments)) {
        printError(parser.errorText());
        return false;
    }
    if (parser.isSet(u"help"_s)) {
        std::print("{}", parser.helpText().toStdString());
        exitSoon(0);
        return true;
    }
    m_diagnostics = QcHeadlessDiagnostics::start(parser, options);
    if (!m_diagnostics) {
        return false;
    }
    m_diagnostics->runCrashTest();
    if (parser.isSet(options.writeDiagnostics)) {
        exitSoon(QcHeadlessDiagnostics::writeBundle(QcHeadlessOptions::dataDirectory(parser, options), {}) ? 0 : 1);
        return true;
    }
    if (parser.isSet(options.icon)) {
        return QcHeadlessTools::exportIcon(parser.value(options.icon));
    }

    const std::optional<QcHeadlessOptions::RunConfig> run = QcHeadlessOptions::readConfig(parser, options);
    if (!run) {
        printError(u"Invalid numeric option or world type"_s);
        return false;
    }
    const QcServerConfig &config = run->server;
    const int timeLimit = run->timeLimit;
    const std::optional<QcModLoader::Result> loaded = QcHeadlessOptions::loadContent(parser, options);
    if (!loaded) {
        return false;
    }
    const std::shared_ptr<const QcContentRegistry> shared = loaded->content;
    if (parser.isSet(options.server)) {
        return startDedicated(parser, *loaded);
    }
    if (parser.isSet(options.join)) {
        return startJoin(parser, *loaded, config.viewDistance, timeLimit);
    }

    if (const QcStatus checked = QcWorldGenerators::checkOptions(config.generator, config.generatorOptions, *shared); !checked) {
        printError(u"Invalid --generator-options: "_s + checked.error());
        return false;
    }
    if (QcHeadlessTools::run(parser, options, config, shared)) {
        return true;
    }
    return startLocal(parser, config, timeLimit, shared);
}

bool QcHeadlessGame::startLocal(const QCommandLineParser &parser, const QcServerConfig &config, int timeLimit,
                                const std::shared_ptr<const QcContentRegistry> &content)
{
    const Options options;
    m_content = content;
    m_baseConfig = config;
    m_library = std::make_unique<QcWorldLibrary>(parser.isSet(options.data) ? QDir(parser.value(options.data)).filePath(u"worlds"_s)
                                                                            : QcWorldLibrary::defaultRoot());
    startStatsLog(QcStatsLog::interval(parser.value(options.statsLog)));
    m_consoleMode = parser.isSet(options.console);
    if (const QcStatus started = startWorld(config); !started) {
        printError(started.error());
        return false;
    }
    if (m_consoleMode) {
        startConsole(parser.isSet(options.time) ? timeLimit : 0, !parser.isSet(options.noEofStop));
        return true;
    }
    startTimeLimit(timeLimit);
    return startControl(parser.isSet(options.script) ? std::optional<QString>(parser.value(options.script)) : std::nullopt);
}

bool QcHeadlessGame::startJoin(const QCommandLineParser &parser, const QcModLoader::Result &content, int viewDistance, int timeLimit)
{
    const Options options;
    m_content = content.content;
    m_remote = true;
    startStatsLog(QcStatsLog::interval(parser.value(options.statsLog)));
    const QString password = parser.isSet(options.password) ? parser.value(options.password) : qEnvironmentVariable("QTCRAFT_SERVER_PASSWORD");
    const QcHeadlessJoin::Options join{parser.value(options.join), parser.value(options.playerName), password,
                                       QcHeadlessOptions::dataDirectory(parser, options), viewDistance};
    const std::optional<QString> script = parser.isSet(options.script) ? std::optional<QString>(parser.value(options.script)) : std::nullopt;

    auto *joining = new QcHeadlessJoin(this);
    connect(joining, &QcHeadlessJoin::failed, this, [](const QString &reason) {
        printError(u"join failed: "_s + reason);
        QCoreApplication::exit(QcHeadlessJoin::exitJoinFailed);
    });
    connect(joining, &QcHeadlessJoin::joined, this, [this, joining, script] {
        if (std::unique_ptr<QcClientSession> session = joining->takeSession()) {
            onJoined(std::move(session), script);
        }
    });
    if (const QcStatus started = joining->start(join, content); !started) {
        printError(started.error());
        return false;
    }

    startTimeLimit(timeLimit);
    return true;
}

void QcHeadlessGame::onJoined(std::unique_ptr<QcClientSession> session, const std::optional<QString> &scriptFile)
{
    m_session = std::move(session);
    printError(u"joined '%1'"_s.arg(m_session->login().serverName()));

    // Only the server ends the connection while the process runs; quitting deletes the session.
    connect(m_session.get(), &QcClientSession::disconnected, this, [](const QString &reason) {
        printError(u"disconnected: "_s + reason);
        QCoreApplication::exit(QcHeadlessJoin::exitConnectionLost);
    });
    if (m_statsLog) {
        m_session->debug().setBaseFlags(QcMsg::DebugSubscribe::stats);
    }
    // The bot's health line names its round trip to the server (decision E-886).
    if (m_diagnostics && m_diagnostics->healthSeconds() > 0) {
        new QcHealthLog(m_diagnostics->healthSeconds(), [this] { return m_session ? QcClientHealth::sample(*m_session) : QcHealthSample{}; }, this);
    }

    if (!startControl(scriptFile)) {
        QCoreApplication::exit(1);
    }
}

bool QcHeadlessGame::startDedicated(const QCommandLineParser &parser, const QcModLoader::Result &content)
{
    const Options options;
    const std::optional<QcServerSettings> settings = QcHeadlessOptions::serverSettings(parser, options);
    if (!settings) {
        return false;
    }

    const QcDedicatedServer::Options run{*settings, QcHeadlessOptions::dataDirectory(parser, options), parser.value(options.generatorOptions),
                                   parser.isSet(options.console), !parser.isSet(options.noEofStop),
                                   parser.isSet(options.time) ? parser.value(options.time).toInt() : 0, m_diagnostics->healthSeconds()};
    m_dedicated = std::make_unique<QcDedicatedServer>(content.content, QcContentFingerprint::of(*content.content, content.mods));
    if (const QcStatus started = m_dedicated->start(run); !started) {
        printError(started.error());
        m_dedicated.reset();
        return false;
    }
    return true;
}

void QcHeadlessGame::startStatsLog(int intervalMs)
{
    if (intervalMs <= 0) {
        return;
    }

    m_statsLog = new QcStatsLog(intervalMs, [this]() -> std::optional<QJsonObject> {
        if (!m_session || m_session->state() != QcClientSession::State::Playing) {
            return std::nullopt;
        }
        return QcDebugJson::toJson(QcDebugSnapshot::collect(*m_session));
    }, this);
}

bool QcHeadlessGame::startControl(const std::optional<QString> &scriptFile)
{
    m_output = new QFile(this);
    // Opened by descriptor: a FILE*-based QFile would sit in stdio's buffer when stdout is a pipe.
    // Standard output is descriptor 1 on every platform (POSIX and the Windows C runtime).
    constexpr int standardOutput = 1;
    if (!m_output->open(standardOutput, QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        printError(u"Cannot open standard output"_s);
        return false;
    }

    m_control = std::make_unique<QcControlInterface>(m_session.get(), nullptr, m_output);
    m_control->setExtensionHandler([this](const QJsonObject &request) { return m_remote ? remoteCommand(request) : worldCommand(request); });
    m_scriptMode = scriptFile.has_value();
    // A script's exit code reports whether every request succeeded, also when it ends with quit.
    if (m_scriptMode) {
        connect(m_control.get(), &QcControlInterface::quitRequested, this, &QcHeadlessGame::finishScript, Qt::QueuedConnection);
    } else {
        connect(m_control.get(), &QcControlInterface::quitRequested, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    }

    if (m_scriptMode) {
        QFile script(*scriptFile);
        if (!script.open(QIODevice::ReadOnly | QIODevice::Text)) {
            printError(u"Cannot open script %1"_s.arg(script.fileName()));
            return false;
        }
        // Queued: by the time this runs all script lines are queued, so idle really means done.
        connect(
            m_control.get(), &QcControlInterface::idle, this,
            [this] {
                if (m_control->isIdle()) {
                    finishScript();
                }
            },
            Qt::QueuedConnection);
        m_control->feed(script.readAll() + '\n');
    } else {
        readStandardInput();
    }
    return true;
}

void QcHeadlessGame::startTimeLimit(int seconds)
{
    QTimer::singleShot(seconds * 1000, this, [] {
        printError(u"time limit reached"_s);
        QCoreApplication::exit(2);
    });
}

void QcHeadlessGame::startConsole(int timeLimit, bool stopAtEndOfInput)
{
    auto *console = new QcServerConsole([this](const QString &line) { return m_server->runConsoleCommand(line); }, stopAtEndOfInput, this);
    connect(console, &QcServerConsole::finished, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    // SIGTERM and Ctrl+C end the process through the event loop: the world is saved (E-255).
    auto *watcher = new QcSignalWatcher(this);
    connect(watcher, &QcSignalWatcher::stopRequested, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    console->start();
    if (timeLimit > 0) {
        QTimer::singleShot(timeLimit * 1000, this, [] {
            printError(u"time limit reached"_s);
            QCoreApplication::exit(2);
        });
    }
}

void QcHeadlessGame::readStandardInput()
{
    auto *reader = new QcStdinReader(this);
    connect(reader, &QcStdinReader::lineReceived, m_control.get(), &QcControlInterface::feed);
    connect(reader, &QcStdinReader::endOfInput, this, [this] {
        // End of input: finish the queued requests, then exit.
        if (m_control->isIdle()) {
            QCoreApplication::quit();
        } else {
            connect(m_control.get(), &QcControlInterface::idle, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
        }
    });
    reader->start();
}

QcStatus QcHeadlessGame::startWorld(const QcServerConfig &config)
{
    // Reopening the open world: stop it completely first so only one server uses the directory.
    // If it cannot be opened again, no world is open until another one is loaded.
    if (m_server && !config.worldDirectory.isEmpty() && config.worldDirectory == m_currentConfig.worldDirectory) {
        if (m_session) {
            m_session->disconnectFromServer();
            // Later, not now: the control interface still points at it while it answers the
            // request that switches the world.
            m_session.release()->deleteLater();
        }

        if (const QcStatus stopped = m_server->shutdown(); !stopped) {
            printError(u"saving the world failed: "_s + stopped.error());
        }

        m_server.reset();
        if (const QcStatus reopened = openWorld(config); !reopened) {
            // No world is open now; requests answer with errors until another world is loaded.
            if (m_control) {
                m_control->setSession(nullptr);
            }
            return reopened;
        }

        return {};
    }

    // Save first so the current world is safe whatever happens next.
    if (m_server) {
        if (const QcStatus saved = m_server->save(); !saved) {
            return qcError(u"Cannot save the current world: %1"_s.arg(saved.error()));
        }
    }

    return openWorld(config);
}

QcStatus QcHeadlessGame::openWorld(const QcServerConfig &config)
{
    std::unique_ptr<QcWorldGenerator> generator = QcWorldGenerators::create(config.generator, m_content, config.seed, config.generatorOptions);
    if (!generator) {
        return qcError(u"Unknown world type '%1'"_s.arg(config.generator));
    }
    for (const QString &warning : generator->warnings()) {
        printError(u"world generation warning: "_s + warning);
    }

    // The current world keeps running when the new one cannot be opened.
    auto server = std::make_unique<QcLocalServerHost>(config, m_content, std::move(generator));
    if (!server->startError().isEmpty()) {
        return qcError(server->startError());
    }

    if (m_session) {
        m_session->disconnectFromServer();
        // Later, not now: the control interface still points at it while it answers the request
        // that switches the world.
        m_session.release()->deleteLater();
    }

    if (m_server) {
        if (const QcStatus stopped = m_server->shutdown(); !stopped) {
            printError(u"saving the previous world failed: "_s + stopped.error());
        }
        m_server.reset();
    }

    m_server = std::move(server);
    m_currentConfig = config;
    connect(m_server.get(), &QcLocalServerHost::stopRequested, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    // The console mode has no control interface, so no stale session pointer remains there.
    if (m_consoleMode) {
        return {};
    }
    m_session = std::make_unique<QcClientSession>(m_server->connectClient(), u"Bot"_s);
    if (m_statsLog) {
        m_session->debug().setBaseFlags(QcMsg::DebugSubscribe::stats);
    }
    connect(m_session.get(), &QcClientSession::disconnected, this, [](const QString &reason) {
        printError(u"disconnected: "_s + reason);
    });
    if (m_control) {
        m_control->setSession(m_session.get());
    }

    // No frames hold this thread up; every tick is applied before the next one runs, so the control
    // interface counts ticks exactly (decision E-560).
    m_session->start(QcClientSession::TickMode::Timer);
    return {};
}

std::optional<QJsonObject> QcHeadlessGame::worldCommand(const QJsonObject &request)
{
    const QString command = request.value(u"cmd"_s).toString();
    if (command == u"worldList"_s) {
        return QJsonObject{{u"worlds"_s, worldList()}};
    }

    if (command == u"worldCreate"_s || command == u"worldLoad"_s) {
        const QcResult<WorldChoice> world = command == u"worldCreate"_s ? createdWorld(request) : listedWorld(request);
        if (!world) {
            return failure(world.error());
        }
        if (const QcStatus started = startWorld(world->config); !started) {
            return failure(started.error());
        }
        return QJsonObject{{u"folder"_s, world->folder}, {u"note"_s, u"Send waitUntilReady before acting in the world."_s}};
    }

    if (command == u"handbook"_s) {
        return handbookCommand(request);
    }

    if (command == u"console"_s) {
        // A line of the server console (decision E-247), with the console's rights.
        if (!m_server || !request.value(u"text"_s).isString()) {
            return failure(m_server ? u"'text' must be a string"_s : u"No world is open"_s);
        }
        return QJsonObject{{u"lines"_s, QJsonArray::fromStringList(m_server->runConsoleCommand(request.value(u"text"_s).toString()))}};
    }

    if (command == u"worldSave"_s) {
        const QcStatus saved = m_server ? m_server->save() : QcStatus(qcError(u"No world is open"_s));
        if (!saved) {
            return failure(saved.error());
        }
        return QJsonObject{{u"saved"_s, true}};
    }

    return std::nullopt;
}

std::optional<QJsonObject> QcHeadlessGame::remoteCommand(const QJsonObject &request)
{
    const QString command = request.value(u"cmd"_s).toString();
    if (command == u"handbook"_s) {
        return handbookCommand(request);
    }

    // A bot on another process's server has no world of its own to manage.
    if (command.startsWith(u"world"_s) || command == u"console"_s) {
        return failure(u"'%1' is not available for a bot that joined a server"_s.arg(command));
    }
    return std::nullopt;
}

QJsonArray QcHeadlessGame::worldList() const
{
    QJsonArray worlds;
    const QList<QcWorldLibrary::Entry> entries = m_library->list();
    for (const QcWorldLibrary::Entry &entry : entries) {
        worlds.append(QJsonObject{{u"folder"_s, entry.folder},
                                  {u"name"_s, entry.info.name},
                                  {u"seed"_s, QString::number(entry.info.seed)},
                                  {u"generator"_s, entry.info.generator},
                                  {u"error"_s, entry.error}});
    }
    return worlds;
}

QcResult<QcHeadlessGame::WorldChoice> QcHeadlessGame::createdWorld(const QJsonObject &request) const
{
    bool seedOk = true;
    const qint64 seed = request.contains(u"seed"_s) ? request.value(u"seed"_s).toVariant().toString().toLongLong(&seedOk)
                                                    : static_cast<qint64>(QRandomGenerator::global()->generate64() >> 1);
    if (!seedOk) {
        return qcError(u"'seed' must be an integer"_s);
    }
    const QString generator = request.value(u"generator"_s).toString(QString::fromLatin1(QcWorldGenerators::terrain));
    const QcResult<QString> options = QcWorldGenerators::optionsToStore(generator, request.value(u"options"_s).toString(), *m_content);
    if (!options) {
        return qcError(options.error());
    }
    const std::optional<QcGameMode> mode = QcGameModes::parse(request.value(u"gameMode"_s).toString(u"survival"_s));
    if (!mode) {
        return qcError(u"'gameMode' must be survival, creative, adventure or spectator"_s);
    }
    const QcWorldLibrary::NewWorld setup{.generator = generator, .options = *options, .gameMode = *mode};
    const QcResult<QcWorldLibrary::Entry> entry = m_library->create(request.value(u"name"_s).toString(u"World"_s), seed, setup);
    if (!entry) {
        return qcError(entry.error());
    }

    QcServerConfig config = m_baseConfig;
    config.worldName = entry->info.name;
    config.seed = entry->info.seed;
    config.generator = entry->info.generator;
    config.generatorOptions = entry->info.generatorOptions;
    config.worldDirectory = entry->directory;
    return WorldChoice{std::move(config), entry->folder};
}

QcResult<QcHeadlessGame::WorldChoice> QcHeadlessGame::listedWorld(const QJsonObject &request) const
{
    const QList<QcWorldLibrary::Entry> entries = m_library->list();
    const QString folder = request.value(u"folder"_s).toString();
    const auto it = std::ranges::find(entries, folder, &QcWorldLibrary::Entry::folder);
    if (it == entries.end()) {
        return qcError(u"No world in folder '%1'"_s.arg(folder));
    }
    if (!it->error.isEmpty()) {
        return qcError(it->error);
    }

    QcServerConfig config = m_baseConfig;
    config.worldName = it->info.name;
    config.seed = it->info.seed;
    config.generator = it->info.generator;
    config.generatorOptions = it->info.generatorOptions;
    config.worldDirectory = it->directory;
    return WorldChoice{std::move(config), folder};
}

const QcHandbook &QcHeadlessGame::handbook(const QString &language)
{
    std::shared_ptr<const QcHandbook> &book = m_handbooks[language];
    if (!book) {
        book = std::make_shared<const QcHandbook>(QcHandbookBuilder::build(*m_content, language, QcHandbookRules::values(*m_content), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys()));
    }
    return *book;
}

QJsonObject QcHeadlessGame::handbookCommand(const QJsonObject &request)
{
    // Only languages the content has get a handbook of their own, so the cache stays small.
    const QString language = request.value(u"language"_s).toString(u"en"_s);
    if (!m_content->translations().languages().contains(language)) {
        return {{u"ok"_s, false},
                {u"error"_s, u"Unknown language '%1' (the content has: %2)"_s.arg(language.left(32), m_content->translations().languages().join(u", "_s))}};
    }

    const QcHandbook &book = handbook(language);
    const auto summaries = [&book](const QList<qsizetype> &indices) {
        QJsonArray list;
        for (const qsizetype index : indices) {
            list.append(QcHandbookJson::summary(book.entries()[index]));
        }
        return list;
    };

    if (request.contains(u"entry"_s)) {
        // An entry id ("block:qtcraft:stone") or a content name ("stone", "qtcraft:zombie").
        QString id = request.value(u"entry"_s).toString();
        const QcHandbookEntry *entry = book.entry(id);
        const QString name = id.contains(u':') ? id : u"qtcraft:"_s + id;
        for (const QString &prefix : {u"block:"_s, u"item:"_s, u"mob:"_s, u"biome:"_s, u"chapter:"_s, u"tag:"_s, u"feature:"_s, u"ore:"_s}) {
            if (entry) {
                break;
            }
            entry = book.entry(prefix + name);
        }

        if (!entry) {
            return {{u"ok"_s, false}, {u"error"_s, u"No handbook entry '%1'"_s.arg(id)}};
        }

        return {{u"entry"_s, QcHandbookJson::entry(book, *entry)}};
    }

    if (request.contains(u"search"_s)) {
        return {{u"results"_s, summaries(book.search(request.value(u"search"_s).toString()))}};
    }

    if (request.contains(u"category"_s)) {
        const QcHandbook::Category *category = book.category(request.value(u"category"_s).toString());
        if (!category) {
            return {{u"ok"_s, false}, {u"error"_s, u"No handbook category '%1'"_s.arg(request.value(u"category"_s).toString())}};
        }
        return {{u"entries"_s, summaries(category->entries)}};
    }

    return {{u"categories"_s, QcHandbookJson::categories(book)},
            {u"entries"_s, book.entries().size()},
            {u"problems"_s, QJsonArray::fromStringList(book.problems())}};
}

void QcHeadlessGame::finishScript()
{
    if (!m_scriptMode) {
        return;
    }
    const int failed = m_control->failedRequests();
    QTimer::singleShot(0, qApp, [failed] { QCoreApplication::exit(failed == 0 ? 0 : 1); });
}
