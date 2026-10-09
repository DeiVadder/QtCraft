#include "QcHeadlessGame.h"

#include "QcAppIcon.h"
#include "QcBaseContent.h"
#include "QcModLoader.h"
#include "QcClientSession.h"
#include "QcControlInterface.h"
#include "QcHandbookBuilder.h"
#include "QcHandbookJson.h"
#include "QcHandbookMarkdown.h"
#include "QcHandbookRules.h"
#include "QcKeyBindings.h"
#include "QcLocalServerHost.h"
#include "QcMapRenderer.h"
#include "QcSoundExporter.h"
#include "QcStdinReader.h"
#include "QcTerrainGenerator.h"
#include "QcTextureAtlas.h"
#include "QcTransport.h"
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
    const QCommandLineOption seedOption(u"seed"_s, u"World seed."_s, u"number"_s, u"0"_s);
    const QCommandLineOption worldOption(u"world"_s, u"World name."_s, u"name"_s, u"Headless"_s);
    const QCommandLineOption viewOption(u"view-distance"_s, u"Chunk radius streamed to the client."_s, u"chunks"_s, u"4"_s);
    const QCommandLineOption scriptOption(u"script"_s, u"Run JSON-lines commands from a file, then exit."_s, u"file"_s);
    const QCommandLineOption timeOption(u"time-limit"_s, u"Exit with failure after this many seconds."_s, u"seconds"_s,
                                        QString::number(defaultTimeLimitSeconds));
    const QCommandLineOption mapOption(u"map"_s, u"Write a top-down terrain map PNG and exit."_s, u"file"_s);
    const QCommandLineOption mapRadiusOption(u"map-radius"_s, u"Map radius in blocks."_s, u"blocks"_s, u"1024"_s);
    const QCommandLineOption biomeMapOption(u"biome-map"_s, u"Write a top-down map with one colour per biome and exit."_s,
                                            u"file"_s);
    const QCommandLineOption spawnOption(u"spawn"_s, u"Spawn above the block column 'x,z' (test hook)."_s, u"x,z"_s);
    const QCommandLineOption sliceOption(u"slice-map"_s,
                                         u"Write a vertical cut through the world along x at z = 0 (--map-radius blocks to each side) and exit."_s,
                                         u"file"_s);
    const QCommandLineOption statsOption(u"world-stats"_s,
                                         u"Print numbers about the generated world (ores, surface, biomes, slopes) and exit."_s);
    const QCommandLineOption statsRadiusOption(u"stats-radius"_s, u"Chunk radius generated for --world-stats."_s, u"chunks"_s,
                                               u"16"_s);
    const QCommandLineOption dataOption(u"data-dir"_s, u"Data directory: worlds in <dir>/worlds, mods in <dir>/mods."_s, u"dir"_s);
    const QCommandLineOption modsOption(u"mods-dir"_s, u"Directory with mods (default: the game's mods directory)."_s, u"dir"_s);
    const QCommandLineOption noModsOption(u"no-mods"_s, u"Load only the base game."_s);
    const QCommandLineOption soundsOption(u"export-sounds"_s,
                                          u"Write every sound and a music piece (seeded by --seed) as WAV files into a directory and exit."_s,
                                          u"dir"_s);
    const QCommandLineOption handbookOption(u"export-handbook"_s,
                                            u"Write the handbook as Markdown files into a directory and exit (base game; with --mods-dir also those mods)."_s,
                                            u"dir"_s);
    const QCommandLineOption languageOption(u"language"_s, u"Language of --export-handbook, e.g. 'de' (default: en)."_s,
                                            u"locale"_s, u"en"_s);
    const QCommandLineOption iconOption(u"export-icon"_s,
                                        u"Write the application icon (base game textures) as the PNG images of a macOS .iconset directory and exit."_s,
                                        u"dir"_s);
    parser.addOptions({soundsOption, iconOption});
    parser.addOptions({handbookOption, languageOption});
    parser.addOptions({statsOption, statsRadiusOption, biomeMapOption, sliceOption, spawnOption});
    parser.addOptions({seedOption, worldOption, viewOption, scriptOption, timeOption, mapOption, mapRadiusOption, dataOption,
                       modsOption, noModsOption});
    if (!parser.parse(arguments)) {
        printError(parser.errorText());
        return false;
    }
    if (parser.isSet(u"help"_s)) {
        std::print("{}", parser.helpText().toStdString());
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(0); });
        return true;
    }
    if (parser.isSet(iconOption)) {
        // Base game only: the icon must not depend on the mods of whoever builds the game.
        QStringList baseWarnings;
        const QcResult<std::shared_ptr<QcContentRegistry>> base = QcBaseContent::load(baseWarnings);
        if (!base) {
            printError(u"Cannot load the base game: "_s + base.error());
            return false;
        }
        const QcStatus written = QcAppIcon::writeIconSet(QcTextureAtlas::build(**base), parser.value(iconOption));
        if (!written) {
            printError(written.error());
        }
        QTimer::singleShot(0, qApp, [ok = written.has_value()] { QCoreApplication::exit(ok ? 0 : 1); });
        return true;
    }

    bool seedOk = false;
    QcServerConfig config;
    config.seed = parser.value(seedOption).toLongLong(&seedOk);
    config.worldName = parser.value(worldOption);
    bool viewOk = false;
    config.viewDistance = std::clamp(parser.value(viewOption).toInt(&viewOk), 1, 32);
    bool timeOk = false;
    const int timeLimit = parser.value(timeOption).toInt(&timeOk);
    bool spawnOk = true;
    if (parser.isSet(spawnOption)) {
        const QStringList parts = parser.value(spawnOption).split(u',');
        bool xOk = false;
        bool zOk = false;
        if (parts.size() == 2) {
            config.spawnColumn = std::array<int, 2>{parts[0].toInt(&xOk), parts[1].toInt(&zOk)};
        }
        spawnOk = xOk && zOk;
    }
    if (!seedOk || !viewOk || !timeOk || timeLimit <= 0 || !spawnOk) {
        printError(u"Invalid numeric option"_s);
        return false;
    }

    QStringList warnings;
    // Mods come from --mods-dir, else from the data directory's mods folder.
    QString modsDirectory = parser.value(modsOption);
    if (modsDirectory.isEmpty()) {
        modsDirectory = parser.isSet(dataOption) ? QDir(parser.value(dataOption)).filePath(u"mods"_s) : QcModLoader::defaultDirectory();
    }
    if (parser.isSet(noModsOption)) {
        modsDirectory.clear();
    }
    QcResult<QcModLoader::Result> content = QcModLoader::load(modsDirectory, warnings);
    for (const QString &warning : std::as_const(warnings)) {
        printError(u"content warning: "_s + warning);
    }
    if (!content) {
        printError(u"Cannot load content: "_s + content.error());
        return false;
    }
    const std::shared_ptr<const QcContentRegistry> shared = std::move(content->content);
    if (parser.isSet(soundsOption)) {
        const QcResult<QStringList> exported = QcSoundExporter::exportAll(*shared, parser.value(soundsOption), quint64(config.seed));
        if (exported) {
            for (const QString &line : *exported) {
                std::println("{}", line.toStdString());
            }
        } else {
            printError(exported.error());
        }
        QTimer::singleShot(0, qApp, [ok = exported.has_value()] { QCoreApplication::exit(ok ? 0 : 1); });
        return true;
    }
    if (parser.isSet(handbookOption)) {
        // The export documents the base game unless mods are asked for with --mods-dir.
        std::shared_ptr<const QcContentRegistry> exported = shared;
        if (!parser.isSet(modsOption)) {
            QStringList baseWarnings;
            QcResult<std::shared_ptr<QcContentRegistry>> base = QcBaseContent::load(baseWarnings);
            if (!base) {
                printError(u"Cannot load the base game: "_s + base.error());
                return false;
            }
            exported = *base;
        }
        const QcHandbook book = QcHandbookBuilder::build(*exported, parser.value(languageOption), QcHandbookRules::values(), QcKeyBindings::defaults(QcKeyBindings::Platform::Other).placeholderKeys());
        for (const QString &problem : book.problems()) {
            printError(u"handbook problem: "_s + problem);
        }
        const QcStatus written = QcHandbookMarkdown::write(book, parser.value(handbookOption));
        if (written) {
            std::println("{} entries in {} files", book.entries().size(), QcHandbookMarkdown::files(book).size());
        } else {
            printError(written.error());
        }
        QTimer::singleShot(0, qApp, [ok = written.has_value()] { QCoreApplication::exit(ok ? 0 : 1); });
        return true;
    }
    QStringList generatorWarnings;
    auto generator = std::make_unique<QcTerrainGenerator>(shared, config.seed, &generatorWarnings);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        printError(u"world generation warning: "_s + warning);
    }
    if (parser.isSet(statsOption)) {
        const int chunks = std::clamp(parser.value(statsRadiusOption).toInt(), 1, 64);
        const int biomeRadius = std::clamp(parser.value(mapRadiusOption).toInt(), 256, 16384);
        const QStringList lines = QcWorldStats::report(*generator, *shared, chunks, biomeRadius);
        for (const QString &line : lines) {
            std::println("{}", line.toStdString());
        }
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(0); });
        return true;
    }
    if (parser.isSet(sliceOption)) {
        const int radius = std::clamp(parser.value(mapRadiusOption).toInt(), 16, 1024);
        const bool saved = QcMapRenderer::renderSlice(*generator, *shared, 0, 0, radius, 4).save(parser.value(sliceOption));
        if (!saved) {
            printError(u"Cannot write map"_s);
        }
        QTimer::singleShot(0, qApp, [saved] { QCoreApplication::exit(saved ? 0 : 1); });
        return true;
    }
    if (parser.isSet(biomeMapOption)) {
        const int radius = std::clamp(parser.value(mapRadiusOption).toInt(), 64, 8192);
        const bool saved =
            QcMapRenderer::renderBiomes(*generator, 0, 0, radius, std::max(1, radius / 512)).save(parser.value(biomeMapOption));
        if (!saved) {
            printError(u"Cannot write map"_s);
        }
        // Legend with the nearest dry column of each biome that lies well inside it.
        for (qsizetype i = 0; i < shared->biomes().size(); ++i) {
            const auto inside = [&](int x, int z) {
                const QcTerrainGenerator::Column column = generator->column(x, z);
                return column.biome == int(i) && column.height > generator->seaLevel() + 1;
            };
            QString where = u"not found"_s;
            for (int ring = 0; ring <= radius && where.startsWith(u'n'); ring += 16) {
                for (int dz = -ring; dz <= ring && where.startsWith(u'n'); dz += 16) {
                    for (int dx = -ring; dx <= ring; dx += 16) {
                        if (std::max(std::abs(dx), std::abs(dz)) == ring && inside(dx, dz) && inside(dx + 32, dz)
                            && inside(dx - 32, dz) && inside(dx, dz + 32) && inside(dx, dz - 32)) {
                            where = u"at %1,%2"_s.arg(dx).arg(dz);
                            break;
                        }
                    }
                }
            }
            std::println("{} {} {}", i, generator->biomeName(int(i)).toStdString(), where.toStdString());
        }
        QTimer::singleShot(0, qApp, [saved] { QCoreApplication::exit(saved ? 0 : 1); });
        return true;
    }
    if (parser.isSet(mapOption)) {
        const int radius = std::clamp(parser.value(mapRadiusOption).toInt(), 64, 8192);
        const QImage map = QcMapRenderer::render(*generator, *shared, 0, 0, radius, std::max(1, radius / 512));
        const bool saved = map.save(parser.value(mapOption));
        if (!saved) {
            printError(u"Cannot write map"_s);
        }
        QTimer::singleShot(0, qApp, [saved] { QCoreApplication::exit(saved ? 0 : 1); });
        return true;
    }
    m_content = shared;
    m_baseConfig = config;
    m_library = std::make_unique<QcWorldLibrary>(parser.isSet(dataOption) ? QDir(parser.value(dataOption)).filePath(u"worlds"_s)
                                                                          : QcWorldLibrary::defaultRoot());
    if (const QcStatus started = startWorld(config); !started) {
        printError(started.error());
        return false;
    }
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
    m_scriptMode = parser.isSet(scriptOption);
    // A script's exit code reports whether every request succeeded, also when it ends with quit.
    if (m_scriptMode) {
        connect(m_control, &QcControlInterface::quitRequested, this, &QcHeadlessGame::finishScript, Qt::QueuedConnection);
    } else {
        connect(m_control, &QcControlInterface::quitRequested, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    }

    if (m_scriptMode) {
        QFile script(parser.value(scriptOption));
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

    QTimer::singleShot(timeLimit * 1000, this, [] {
        printError(u"time limit reached"_s);
        QCoreApplication::exit(2);
    });
    return true;
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
    auto generator = std::make_unique<QcTerrainGenerator>(m_content, config.seed, &generatorWarnings);
    for (const QString &warning : std::as_const(generatorWarnings)) {
        printError(u"world generation warning: "_s + warning);
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
    m_session = new QcClientSession(m_server->connectClient(), u"Bot"_s, this);
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
    const auto failWith = [&response](const QString &error) {
        response = {{u"ok"_s, false}, {u"error"_s, error}};
        return true;
    };
    if (command == u"worldList"_s) {
        QJsonArray worlds;
        const QList<QcWorldLibrary::Entry> entries = m_library->list();
        for (const QcWorldLibrary::Entry &entry : entries) {
            worlds.append(QJsonObject{{u"folder"_s, entry.folder},
                                      {u"name"_s, entry.info.name},
                                      {u"seed"_s, QString::number(entry.info.seed)},
                                      {u"error"_s, entry.error}});
        }
        response = {{u"worlds"_s, worlds}};
        return true;
    }
    if (command == u"worldCreate"_s || command == u"worldLoad"_s) {
        QcServerConfig config = m_baseConfig;
        if (command == u"worldCreate"_s) {
            bool seedOk = true;
            const qint64 seed = request.contains(u"seed"_s)
                                    ? request.value(u"seed"_s).toVariant().toString().toLongLong(&seedOk)
                                    : qint64(QRandomGenerator::global()->generate64() >> 1);
            if (!seedOk) {
                return failWith(u"'seed' must be an integer"_s);
            }
            const QcResult<QcWorldLibrary::Entry> entry = m_library->create(request.value(u"name"_s).toString(u"World"_s), seed);
            if (!entry) {
                return failWith(entry.error());
            }
            config.worldName = entry->info.name;
            config.seed = entry->info.seed;
            config.worldDirectory = entry->directory;
            response = {{u"folder"_s, entry->folder}};
        } else {
            const QList<QcWorldLibrary::Entry> entries = m_library->list();
            const QString folder = request.value(u"folder"_s).toString();
            const auto it = std::ranges::find(entries, folder, &QcWorldLibrary::Entry::folder);
            if (it == entries.end()) {
                return failWith(u"No world in folder '%1'"_s.arg(folder));
            }
            if (!it->error.isEmpty()) {
                return failWith(it->error);
            }
            config.worldName = it->info.name;
            config.seed = it->info.seed;
            config.worldDirectory = it->directory;
            response = {{u"folder"_s, folder}};
        }
        if (const QcStatus started = startWorld(config); !started) {
            return failWith(started.error());
        }
        response.insert(u"note"_s, u"Send waitUntilReady before acting in the world."_s);
        return true;
    }
    if (command == u"handbook"_s) {
        handleHandbookCommand(request, response);
        return true;
    }
    if (command == u"worldSave"_s) {
        const QcStatus saved = m_server ? m_server->save() : QcStatus(qcError(u"No world is open"_s));
        if (!saved) {
            return failWith(saved.error());
        }
        response = {{u"saved"_s, true}};
        return true;
    }
    return false;
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
