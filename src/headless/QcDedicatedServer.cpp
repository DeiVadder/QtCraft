#include "headless/QcDedicatedServer.h"

#include "headless/QcServerConsole.h"
#include "headless/QcSignalWatcher.h"
#include "server/core/QcLocalServerHost.h"
#include "server/storage/QcWorldLibrary.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QCoreApplication>
#include <QDir>
#include <QRandomGenerator>
#include <QTimer>

#include <cstdio>
#include <print>

using namespace Qt::StringLiterals;

namespace {

// Lines for people and scripts watching the server; flushed at once (stdout may be a pipe).
void say(const QString &line)
{
    std::println("{}", line.toStdString());
    std::fflush(stdout);
}

} // namespace

QcDedicatedServer::QcDedicatedServer(std::shared_ptr<const QcContentRegistry> content, QList<QcPackFingerprint> packs, QObject *parent)
    : QObject(parent)
    , m_content(std::move(content))
    , m_packs(std::move(packs))
{
}

QcDedicatedServer::~QcDedicatedServer()
{
    if (!m_host) {
        return;
    }

    if (const QcStatus stopped = m_host->shutdown(); !stopped) {
        std::println(stderr, "Saving the world failed: {}", stopped.error().toStdString());
        return;
    }
    say(u"World saved, server stopped"_s);
}

QcStatus QcDedicatedServer::start(const Options &options)
{
    const QcResult<QcServerConfig> config = worldConfig(options);
    if (!config) {
        return qcError(config.error());
    }

    QStringList warnings;
    std::unique_ptr<QcWorldGenerator> generator =
        QcWorldGenerators::create(config->generator, m_content, config->seed, &warnings, config->generatorOptions);
    for (const QString &warning : std::as_const(warnings)) {
        std::println(stderr, "world generation warning: {}", warning.toStdString());
    }
    if (!generator) {
        return qcError(u"Unknown world type '%1'"_s.arg(config->generator));
    }

    m_host = std::make_unique<QcLocalServerHost>(*config, m_content, std::move(generator));
    if (!m_host->startError().isEmpty()) {
        const QString error = m_host->startError();
        m_host.reset();
        return qcError(u"Cannot open the world: %1"_s.arg(error));
    }
    connect(m_host.get(), &QcLocalServerHost::stopRequested, this, [this] { stop(u"/stop"_s, 0); });

    const QcResult<quint16> port = m_host->listen(options.settings.networkConfig());
    if (!port) {
        return qcError(port.error());
    }

    // Before the first line that tells scripts the server is ready.
    m_signals = new QcSignalWatcher(this);
    connect(m_signals, &QcSignalWatcher::stopRequested, this, [this](const QString &name) { stop(name, 0); });

    say(u"Server \"%1\" with world \"%2\" (%3), up to %4 players"_s.arg(config->serverName, config->worldName, QDir(config->worldDirectory).dirName())
            .arg(config->maxPlayers));
    say(u"Listening on port %1"_s.arg(*port));

    if (options.console) {
        startConsole(options.stopAtEndOfInput);
    }
    if (options.timeLimit > 0) {
        QTimer::singleShot(options.timeLimit * 1000, this, [this] { stop(u"time limit"_s, 2); });
    }
    return {};
}

QcResult<QcServerConfig> QcDedicatedServer::worldConfig(const Options &options) const
{
    const QcServerSettings &settings = options.settings;
    if (!QcWorldGenerators::isKnown(settings.worldType)) {
        return qcError(u"Unknown world type '%1'"_s.arg(settings.worldType));
    }

    const QcWorldLibrary library(QDir(options.dataDirectory).filePath(u"worlds"_s));
    const QList<QcWorldLibrary::Entry> worlds = library.list();
    const auto found = std::ranges::find(worlds, settings.world, &QcWorldLibrary::Entry::folder);
    QcWorldLibrary::Entry entry;
    if (found != worlds.end()) {
        if (!found->error.isEmpty()) {
            return qcError(found->error);
        }
        entry = *found;
    } else {
        const QcResult<QString> stored = QcWorldGenerators::optionsToStore(settings.worldType, options.generatorOptions, *m_content);
        if (!stored) {
            return qcError(stored.error());
        }
        const qint64 seed = settings.seed.value_or(qint64(QRandomGenerator::global()->generate64() >> 1));
        const QcWorldLibrary::NewWorld setup{.generator = settings.worldType,
                                             .options = *stored,
                                             .gameMode = settings.defaultGameMode.value_or(QcGameMode::Survival)};
        const QcResult<QcWorldLibrary::Entry> created = library.create(settings.world, seed, setup);
        if (!created) {
            return qcError(created.error());
        }
        say(u"Created the world \"%1\" (seed %2)"_s.arg(created->info.name).arg(created->info.seed));
        entry = *created;
    }

    QcServerConfig config;
    config.worldName = entry.info.name;
    config.seed = entry.info.seed;
    config.generator = entry.info.generator;
    config.generatorOptions = entry.info.generatorOptions;
    config.worldDirectory = entry.directory;
    settings.applyTo(config);
    config.contentPacks = m_packs;
    return config;
}

void QcDedicatedServer::startConsole(bool stopAtEndOfInput)
{
    auto *console = new QcServerConsole([this](const QString &line) { return m_host->runConsoleCommand(line); }, stopAtEndOfInput, this);
    connect(console, &QcServerConsole::finished, this, [this] { stop(u"end of input"_s, 0); });
    console->start();
}

void QcDedicatedServer::stop(const QString &why, int code)
{
    say(u"Stopping (%1)"_s.arg(why));
    QTimer::singleShot(0, qApp, [code] { QCoreApplication::exit(code); });
}
