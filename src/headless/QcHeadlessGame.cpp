#include "QcHeadlessGame.h"

#include "QcAppIcon.h"
#include "QcBaseContent.h"
#include "QcModLoader.h"
#include "QcClientSession.h"
#include "QcControlInterface.h"
#include "QcDebugJson.h"
#include "QcHandbookBuilder.h"
#include "QcHandbookJson.h"
#include "QcHandbookMarkdown.h"
#include "QcHandbookRules.h"
#include "QcKeyBindings.h"
#include "QcLocalServerHost.h"
#include "QcMapRenderer.h"
#include "QcServerConsole.h"
#include "QcShowcaseGenerator.h"
#include "QcShowcaseTour.h"
#include "QcSoundExporter.h"
#include "QcStatsLog.h"
#include "QcStdinReader.h"
#include "QcTerrainGenerator.h"
#include "QcTextureAtlas.h"
#include "QcTransport.h"
#include "QcWorldGenerators.h"
#include "QcWorldLibrary.h"
#include "QcWorldStats.h"

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

constexpr int defaultTimeLimitSeconds = 120;

void printError(const QString &message)
{
    std::println(stderr, "{}", message.toStdString());
}

void printLines(const QStringList &lines)
{
    for (const QString &line : lines) {
        std::println("{}", line.toStdString());
    }
}

// Ends the application with `code` once the event loop runs.
void exitSoon(int code)
{
    QTimer::singleShot(0, qApp, [code] { QCoreApplication::exit(code); });
}

bool fail(QJsonObject &response, const QString &error)
{
    response = {{u"ok"_s, false}, {u"error"_s, error}};
    return true;
}

// The command line options of qtcraft-headless.
struct Options
{
    QCommandLineOption seed{u"seed"_s, u"World seed."_s, u"number"_s, u"0"_s};
    QCommandLineOption world{u"world"_s, u"World name."_s, u"name"_s, u"Headless"_s};
    QCommandLineOption view{u"view-distance"_s, u"Chunk radius streamed to the client."_s, u"chunks"_s, u"4"_s};
    QCommandLineOption script{u"script"_s, u"Run JSON-lines commands from a file, then exit."_s, u"file"_s};
    QCommandLineOption time{u"time-limit"_s, u"Exit with failure after this many seconds."_s, u"seconds"_s,
                            QString::number(defaultTimeLimitSeconds)};
    QCommandLineOption map{u"map"_s, u"Write a top-down terrain map PNG and exit."_s, u"file"_s};
    QCommandLineOption mapRadius{u"map-radius"_s, u"Map radius in blocks."_s, u"blocks"_s, u"1024"_s};
    QCommandLineOption biomeMap{u"biome-map"_s, u"Write a top-down map with one colour per biome and exit."_s, u"file"_s};
    QCommandLineOption spawn{u"spawn"_s, u"Spawn above the block column 'x,z' (test hook)."_s, u"x,z"_s};
    QCommandLineOption slice{u"slice-map"_s,
                             u"Write a vertical cut through the world along x at z = 0 (--map-radius blocks to each side) and exit."_s,
                             u"file"_s};
    QCommandLineOption stats{u"world-stats"_s, u"Print numbers about the generated world (ores, surface, biomes, slopes) and exit."_s};
    QCommandLineOption statsRadius{u"stats-radius"_s, u"Chunk radius generated for --world-stats."_s, u"chunks"_s, u"16"_s};
    QCommandLineOption data{u"data-dir"_s, u"Data directory: worlds in <dir>/worlds, mods in <dir>/mods."_s, u"dir"_s};
    QCommandLineOption mods{u"mods-dir"_s, u"Directory with mods (default: the game's mods directory)."_s, u"dir"_s};
    QCommandLineOption noMods{u"no-mods"_s, u"Load only the base game."_s};
    QCommandLineOption sounds{u"export-sounds"_s,
                              u"Write every sound and a music piece (seeded by --seed) as WAV files into a directory and exit."_s,
                              u"dir"_s};
    QCommandLineOption handbook{u"export-handbook"_s,
                                u"Write the handbook as Markdown files into a directory and exit (base game; with --mods-dir also those mods)."_s,
                                u"dir"_s};
    QCommandLineOption language{u"language"_s, u"Language of --export-handbook, e.g. 'de' (default: en)."_s, u"locale"_s, u"en"_s};
    QCommandLineOption generator{u"generator"_s, u"World type of the world started at once: terrain, flat, void or showcase."_s, u"type"_s,
                                 u"terrain"_s};
    QCommandLineOption generatorOptions{u"generator-options"_s,
                                        u"Options of --generator: the layers of a flat world ('bedrock,3*dirt,grass_block') or the platform of a void world ('platform=glass,radius=4,y=40')."_s,
                                        u"options"_s};
    QCommandLineOption tour{u"showcase-tour"_s,
                            u"Print the camera stops of the showcase tour ('name x z yaw pitch' per line) and exit."_s};
    QCommandLineOption statsLog{u"stats"_s,
                                u"Log the debug data as a JSON line 'STATS {...}' every this many milliseconds (also QTCRAFT_STATS)."_s,
                                u"ms"_s};
    QCommandLineOption console{u"console"_s,
                               u"Run a server console on standard input instead of the control interface (no bot joins; "
                               u"the time limit applies only when given)."_s};
    QCommandLineOption noEofStop{u"no-eof-stop"_s,
                                 u"With --console: the end of standard input does not stop the server (a service without input); "
                                 u"it runs until the time limit or until the process is ended."_s};
    QCommandLineOption icon{u"export-icon"_s,
                            u"Write the application icon (base game textures) as the PNG images of a macOS .iconset directory and exit."_s,
                            u"dir"_s};

    // In the order of the help text.
    void addTo(QCommandLineParser &parser) const
    {
        parser.addOptions({sounds, icon});
        parser.addOptions({handbook, language});
        parser.addOptions({stats, statsRadius, biomeMap, slice, spawn, tour});
        parser.addOptions({seed, world, generator, generatorOptions, view, script, console, noEofStop, time, map, mapRadius, data, mods, noMods, statsLog});
    }
};

// Base game only: the icon must not depend on the mods of whoever builds the game.
bool exportIcon(const QString &directory)
{
    QStringList baseWarnings;
    const QcResult<std::shared_ptr<QcContentRegistry>> base = QcBaseContent::load(baseWarnings);
    if (!base) {
        printError(u"Cannot load the base game: "_s + base.error());
        return false;
    }

    const QcStatus written = QcAppIcon::writeIconSet(QcTextureAtlas::build(**base), directory);
    if (!written) {
        printError(written.error());
    }
    exitSoon(written ? 0 : 1);
    return true;
}

// Seed, world name, view distance, spawn column and the time limit; false on a bad number.
bool readConfig(const QCommandLineParser &parser, const Options &options, QcServerConfig &config, int &timeLimit)
{
    bool seedOk = false;
    config.seed = parser.value(options.seed).toLongLong(&seedOk);
    config.worldName = parser.value(options.world);
    config.generator = parser.value(options.generator);
    config.generatorOptions = parser.value(options.generatorOptions);
    bool viewOk = false;
    config.viewDistance = std::clamp(parser.value(options.view).toInt(&viewOk), 1, 32);
    bool timeOk = false;
    timeLimit = parser.value(options.time).toInt(&timeOk);

    bool spawnOk = true;
    if (parser.isSet(options.spawn)) {
        const QStringList parts = parser.value(options.spawn).split(u',');
        bool xOk = false;
        bool zOk = false;
        if (parts.size() == 2) {
            config.spawnColumn = std::array<int, 2>{parts[0].toInt(&xOk), parts[1].toInt(&zOk)};
        }
        spawnOk = xOk && zOk;
    }
    return seedOk && viewOk && timeOk && timeLimit > 0 && spawnOk && QcWorldGenerators::isKnown(config.generator);
}

// The base game with the mods of --mods-dir, else of the data directory's mods folder; nullptr
// (after printing the reason) when it cannot be loaded.
std::shared_ptr<const QcContentRegistry> loadContent(const QCommandLineParser &parser, const Options &options)
{
    QStringList warnings;
    QString modsDirectory = parser.value(options.mods);
    if (modsDirectory.isEmpty()) {
        modsDirectory = parser.isSet(options.data) ? QDir(parser.value(options.data)).filePath(u"mods"_s) : QcModLoader::defaultDirectory();
    }
    if (parser.isSet(options.noMods)) {
        modsDirectory.clear();
    }

    QcResult<QcModLoader::Result> content = QcModLoader::load(modsDirectory, warnings);
    for (const QString &warning : std::as_const(warnings)) {
        printError(u"content warning: "_s + warning);
    }
    if (!content) {
        printError(u"Cannot load content: "_s + content.error());
        return nullptr;
    }
    return std::move(content->content);
}

void exportSounds(const QcContentRegistry &content, const QString &directory, quint64 seed)
{
    const QcResult<QStringList> exported = QcSoundExporter::exportAll(content, directory, seed);
    if (exported) {
        printLines(*exported);
    } else {
        printError(exported.error());
    }
    exitSoon(exported ? 0 : 1);
}

// The export documents the base game unless mods are asked for with --mods-dir.
bool exportHandbook(const QCommandLineParser &parser, const Options &options, const std::shared_ptr<const QcContentRegistry> &content)
{
    std::shared_ptr<const QcContentRegistry> exported = content;
    if (!parser.isSet(options.mods)) {
        QStringList baseWarnings;
        QcResult<std::shared_ptr<QcContentRegistry>> base = QcBaseContent::load(baseWarnings);
        if (!base) {
            printError(u"Cannot load the base game: "_s + base.error());
            return false;
        }
        exported = *base;
    }

    const QcHandbook book = QcHandbookBuilder::build(*exported, parser.value(options.language), QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
    for (const QString &problem : book.problems()) {
        printError(u"handbook problem: "_s + problem);
    }
    const QcStatus written = QcHandbookMarkdown::write(book, parser.value(options.handbook));
    if (written) {
        std::println("{} entries in {} files", book.entries().size(), QcHandbookMarkdown::files(book).size());
    } else {
        printError(written.error());
    }
    exitSoon(written ? 0 : 1);
    return true;
}

void saveMap(const QImage &map, const QString &file)
{
    const bool saved = map.save(file);
    if (!saved) {
        printError(u"Cannot write map"_s);
    }
    exitSoon(saved ? 0 : 1);
}

// Whether a dry column and the columns 32 blocks around it lie in `biome`.
bool wellInside(const QcTerrainGenerator &generator, int biome, int x, int z)
{
    const auto inside = [&](int cx, int cz) {
        const QcTerrainGenerator::Column column = generator.column(cx, cz);
        return column.biome == biome && column.height > generator.seaLevel() + 1;
    };
    return inside(x, z) && inside(x + 32, z) && inside(x - 32, z) && inside(x, z + 32) && inside(x, z - 32);
}

// The first column well inside `biome` on the square ring of `ring` blocks around the origin.
std::optional<QString> legendPlaceOnRing(const QcTerrainGenerator &generator, int biome, int ring)
{
    for (int dz = -ring; dz <= ring; dz += 16) {
        for (int dx = -ring; dx <= ring; dx += 16) {
            if (std::max(std::abs(dx), std::abs(dz)) == ring && wellInside(generator, biome, dx, dz)) {
                return u"at %1,%2"_s.arg(dx).arg(dz);
            }
        }
    }
    return std::nullopt;
}

// The nearest dry column of a biome that lies well inside it, for the biome map's legend.
QString biomeLegendPlace(const QcTerrainGenerator &generator, int biome, int radius)
{
    for (int ring = 0; ring <= radius; ring += 16) {
        if (const std::optional<QString> place = legendPlaceOnRing(generator, biome, ring)) {
            return *place;
        }
    }
    return u"not found"_s;
}

void writeBiomeMap(const QcTerrainGenerator &generator, const QcContentRegistry &content, int radius, const QString &file)
{
    const bool saved = QcMapRenderer::renderBiomes(generator, {0, 0, radius, std::max(1, radius / 512)}).save(file);
    if (!saved) {
        printError(u"Cannot write map"_s);
    }
    for (qsizetype i = 0; i < content.biomes().size(); ++i) {
        std::println("{} {} {}", i, generator.biomeName(int(i)).toStdString(), biomeLegendPlace(generator, int(i), radius).toStdString());
    }
    exitSoon(saved ? 0 : 1);
}

// The world generation tools (--world-stats, --slice-map, --biome-map, --map); false when none
// was asked for.
bool runWorldTool(const QCommandLineParser &parser, const Options &options, const QcTerrainGenerator &generator,
                  const QcContentRegistry &content)
{
    if (parser.isSet(options.stats)) {
        const int chunks = std::clamp(parser.value(options.statsRadius).toInt(), 1, 64);
        const int biomeRadius = std::clamp(parser.value(options.mapRadius).toInt(), 256, 16384);
        printLines(QcWorldStats::report(generator, content, chunks, biomeRadius));
        exitSoon(0);
        return true;
    }

    if (parser.isSet(options.slice)) {
        const int radius = std::clamp(parser.value(options.mapRadius).toInt(), 16, 1024);
        saveMap(QcMapRenderer::renderSlice(generator, content, {0, 0, radius, 4}), parser.value(options.slice));
        return true;
    }

    if (parser.isSet(options.biomeMap)) {
        const int radius = std::clamp(parser.value(options.mapRadius).toInt(), 64, 8192);
        writeBiomeMap(generator, content, radius, parser.value(options.biomeMap));
        return true;
    }

    if (parser.isSet(options.map)) {
        const int radius = std::clamp(parser.value(options.mapRadius).toInt(), 64, 8192);
        saveMap(QcMapRenderer::render(generator, content, {0, 0, radius, std::max(1, radius / 512)}), parser.value(options.map));
        return true;
    }

    return false;
}

} // namespace

QcHeadlessGame::QcHeadlessGame(QObject *parent)
    : QObject(parent)
{
}

QcHeadlessGame::~QcHeadlessGame()
{
    delete m_control;
    delete m_session;
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
    if (parser.isSet(options.icon)) {
        return exportIcon(parser.value(options.icon));
    }

    QcServerConfig config;
    int timeLimit = 0;
    if (!readConfig(parser, options, config, timeLimit)) {
        printError(u"Invalid numeric option or world type"_s);
        return false;
    }
    const std::shared_ptr<const QcContentRegistry> shared = loadContent(parser, options);
    if (!shared) {
        return false;
    }
    if (const QcStatus checked = QcWorldGenerators::checkOptions(config.generator, config.generatorOptions, *shared); !checked) {
        printError(u"Invalid --generator-options: "_s + checked.error());
        return false;
    }
    if (parser.isSet(options.sounds)) {
        exportSounds(*shared, parser.value(options.sounds), quint64(config.seed));
        return true;
    }
    if (parser.isSet(options.handbook)) {
        return exportHandbook(parser, options, shared);
    }

    if (parser.isSet(options.tour)) {
        printLines(QcShowcaseTour::lines(QcShowcaseGenerator(shared).plan()));
        exitSoon(0);
        return true;
    }

    QStringList generatorWarnings;
    const QcTerrainGenerator generator(shared, config.seed, &generatorWarnings);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        printError(u"world generation warning: "_s + warning);
    }
    if (runWorldTool(parser, options, generator, *shared)) {
        return true;
    }

    m_content = shared;
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
    return startControl(parser.isSet(options.script) ? std::optional<QString>(parser.value(options.script)) : std::nullopt, timeLimit);
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

bool QcHeadlessGame::startControl(const std::optional<QString> &scriptFile, int timeLimit)
{
    m_output = new QFile(this);
    // Opened by descriptor: a FILE*-based QFile would sit in stdio's buffer when stdout is a pipe.
    if (!m_output->open(fileno(stdout), QIODevice::WriteOnly | QIODevice::Unbuffered)) {
        printError(u"Cannot open standard output"_s);
        return false;
    }

    m_control = new QcControlInterface(m_session, nullptr, m_output);
    m_control->setExtensionHandler([this](const QJsonObject &request, QJsonObject &response) {
        return handleWorldCommand(request, response);
    });
    m_scriptMode = scriptFile.has_value();
    // A script's exit code reports whether every request succeeded, also when it ends with quit.
    if (m_scriptMode) {
        connect(m_control, &QcControlInterface::quitRequested, this, &QcHeadlessGame::finishScript, Qt::QueuedConnection);
    } else {
        connect(m_control, &QcControlInterface::quitRequested, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    }

    if (m_scriptMode) {
        QFile script(*scriptFile);
        if (!script.open(QIODevice::ReadOnly | QIODevice::Text)) {
            printError(u"Cannot open script %1"_s.arg(script.fileName()));
            return false;
        }
        // Queued: by the time this runs all script lines are queued, so idle really means done.
        connect(
            m_control, &QcControlInterface::idle, this,
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

    QTimer::singleShot(timeLimit * 1000, this, [] {
        printError(u"time limit reached"_s);
        QCoreApplication::exit(2);
    });
    return true;
}

void QcHeadlessGame::startConsole(int timeLimit, bool stopAtEndOfInput)
{
    auto *console = new QcServerConsole([this](const QString &line) { return m_server->runConsoleCommand(line); }, stopAtEndOfInput, this);
    connect(console, &QcServerConsole::finished, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
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
    connect(reader, &QcStdinReader::lineReceived, m_control, &QcControlInterface::feed);
    connect(reader, &QcStdinReader::endOfInput, this, [this] {
        // End of input: finish the queued requests, then exit.
        if (m_control->isIdle()) {
            QCoreApplication::quit();
        } else {
            connect(m_control, &QcControlInterface::idle, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
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
            m_session->deleteLater();
            m_session = nullptr;
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
    QStringList generatorWarnings;
    std::unique_ptr<QcWorldGenerator> generator =
        QcWorldGenerators::create(config.generator, m_content, config.seed, &generatorWarnings, config.generatorOptions);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        printError(u"world generation warning: "_s + warning);
    }
    if (!generator) {
        return qcError(u"Unknown world type '%1'"_s.arg(config.generator));
    }

    // The current world keeps running when the new one cannot be opened.
    auto server = std::make_unique<QcLocalServerHost>(config, m_content, std::move(generator));
    if (!server->startError().isEmpty()) {
        return qcError(server->startError());
    }

    if (m_session) {
        m_session->disconnectFromServer();
        m_session->deleteLater();
        m_session = nullptr;
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
    if (m_consoleMode) {
        return {};
    }
    m_session = new QcClientSession(m_server->connectClient(), u"Bot"_s, this);
    if (m_statsLog) {
        m_session->setDebugSubscriptionBase(QcMsg::DebugSubscribe::stats);
    }
    connect(m_session, &QcClientSession::disconnected, this, [](const QString &reason) {
        printError(u"disconnected: "_s + reason);
    });
    if (m_control) {
        m_control->setSession(m_session);
    }

    m_session->start();
    return {};
}

bool QcHeadlessGame::handleWorldCommand(const QJsonObject &request, QJsonObject &response)
{
    const QString command = request.value(u"cmd"_s).toString();
    if (command == u"worldList"_s) {
        response = {{u"worlds"_s, worldList()}};
        return true;
    }

    if (command == u"worldCreate"_s || command == u"worldLoad"_s) {
        const QcResult<QcServerConfig> config = command == u"worldCreate"_s ? createdWorld(request, response) : listedWorld(request, response);
        if (!config) {
            return fail(response, config.error());
        }
        if (const QcStatus started = startWorld(*config); !started) {
            return fail(response, started.error());
        }
        response.insert(u"note"_s, u"Send waitUntilReady before acting in the world."_s);
        return true;
    }

    if (command == u"handbook"_s) {
        handleHandbookCommand(request, response);
        return true;
    }

    if (command == u"console"_s) {
        // A line of the server console (decision E-247), with the console's rights.
        if (!m_server || !request.value(u"text"_s).isString()) {
            return fail(response, m_server ? u"'text' must be a string"_s : u"No world is open"_s);
        }
        response = {{u"lines"_s, QJsonArray::fromStringList(m_server->runConsoleCommand(request.value(u"text"_s).toString()))}};
        return true;
    }

    if (command == u"worldSave"_s) {
        const QcStatus saved = m_server ? m_server->save() : QcStatus(qcError(u"No world is open"_s));
        if (!saved) {
            return fail(response, saved.error());
        }
        response = {{u"saved"_s, true}};
        return true;
    }

    return false;
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

QcResult<QcServerConfig> QcHeadlessGame::createdWorld(const QJsonObject &request, QJsonObject &response) const
{
    bool seedOk = true;
    const qint64 seed = request.contains(u"seed"_s) ? request.value(u"seed"_s).toVariant().toString().toLongLong(&seedOk)
                                                    : qint64(QRandomGenerator::global()->generate64() >> 1);
    if (!seedOk) {
        return qcError(u"'seed' must be an integer"_s);
    }
    const QString generator = request.value(u"generator"_s).toString(QString::fromLatin1(QcWorldGenerators::terrain));
    const QcResult<QString> options = QcWorldGenerators::optionsToStore(generator, request.value(u"options"_s).toString(), *m_content);
    if (!options) {
        return qcError(options.error());
    }
    const QcResult<QcWorldLibrary::Entry> entry = m_library->create(request.value(u"name"_s).toString(u"World"_s), seed, true, generator, *options);
    if (!entry) {
        return qcError(entry.error());
    }

    QcServerConfig config = m_baseConfig;
    config.worldName = entry->info.name;
    config.seed = entry->info.seed;
    config.generator = entry->info.generator;
    config.generatorOptions = entry->info.generatorOptions;
    config.worldDirectory = entry->directory;
    response = {{u"folder"_s, entry->folder}};
    return config;
}

QcResult<QcServerConfig> QcHeadlessGame::listedWorld(const QJsonObject &request, QJsonObject &response) const
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
    response = {{u"folder"_s, folder}};
    return config;
}

const QcHandbook &QcHeadlessGame::handbook(const QString &language)
{
    std::shared_ptr<const QcHandbook> &book = m_handbooks[language];
    if (!book) {
        book = std::make_shared<const QcHandbook>(QcHandbookBuilder::build(*m_content, language, QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys()));
    }
    return *book;
}

void QcHeadlessGame::handleHandbookCommand(const QJsonObject &request, QJsonObject &response)
{
    // Only languages the content has get a handbook of their own, so the cache stays small.
    const QString language = request.value(u"language"_s).toString(u"en"_s);
    if (!m_content->languages().contains(language)) {
        response = {{u"ok"_s, false},
                    {u"error"_s, u"Unknown language '%1' (the content has: %2)"_s.arg(language.left(32), m_content->languages().join(u", "_s))}};
        return;
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
            response = {{u"ok"_s, false}, {u"error"_s, u"No handbook entry '%1'"_s.arg(id)}};
            return;
        }

        response = {{u"entry"_s, QcHandbookJson::entry(book, *entry)}};
        return;
    }

    if (request.contains(u"search"_s)) {
        response = {{u"results"_s, summaries(book.search(request.value(u"search"_s).toString()))}};
        return;
    }

    if (request.contains(u"category"_s)) {
        const QcHandbook::Category *category = book.category(request.value(u"category"_s).toString());
        if (!category) {
            response = {{u"ok"_s, false}, {u"error"_s, u"No handbook category '%1'"_s.arg(request.value(u"category"_s).toString())}};
            return;
        }
        response = {{u"entries"_s, summaries(category->entries)}};
        return;
    }

    response = {{u"categories"_s, QcHandbookJson::categories(book)},
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
