#include "headless/QcDedicatedServer.h"

#include "common/core/QcLogging.h"
#include "common/diagnostics/QcDiagnosticsBundle.h"
#include "common/diagnostics/QcHealthLog.h"
#include "common/diagnostics/QcMessageLog.h"
#include "headless/QcHeadlessOptions.h"
#include "headless/QcServerConsole.h"
#include "headless/QcSignalWatcher.h"
#include "network/tls/QcTlsCredentials.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"
#include "worlds/QcLocalServerHost.h"
#include "worlds/QcWorldLibrary.h"

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
    QcMessageLog::note("qtcraft.headless", line);
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

    // The generator and the server see the content with the world's options set (decision E-917).
    const QcWorldOptions::Applied applied = QcWorldOptions::apply(m_content, config->generator, config->worldOptions);
    std::unique_ptr<QcWorldGenerator> generator = QcWorldGenerators::create(config->generator, applied.content, config->seed, config->generatorOptions);
    if (!generator) {
        return qcError(u"Unknown world type '%1'"_s.arg(config->generator));
    }
    const QStringList warnings = applied.warnings + generator->warnings();
    for (const QString &warning : warnings) {
        std::println(stderr, "world generation warning: {}", warning.toStdString());
    }

    m_host = std::make_unique<QcLocalServerHost>(*config, applied.content, std::move(generator));
    if (!m_host->startError().isEmpty()) {
        const QString error = m_host->startError();
        m_host.reset();
        return qcError(u"Cannot open the world: %1"_s.arg(error));
    }
    connect(m_host.get(), &QcLocalServerHost::stopRequested, this, [this] { stop(u"/stop"_s, 0); });

    // The certificate players pin (decision E-872), made at the first start.
    QcNetworkConfig network = options.settings.networkConfig();
    QcResult<QcTlsCredentials> tls = QcTlsCredentials::loadOrCreate(options.dataDirectory);
    if (!tls) {
        return qcError(tls.error());
    }
    network.tls = std::move(*tls);
    const QcResult<quint16> port = m_host->listen(network);
    if (!port) {
        return qcError(port.error());
    }

    // Before the first line that tells scripts the server is ready.
    m_signals = new QcSignalWatcher(this);
    connect(m_signals, &QcSignalWatcher::stopRequested, this, [this](const QString &name) { stop(name, 0); });

    say(u"Server \"%1\" with world \"%2\" (%3), up to %4 players"_s.arg(config->serverName, config->worldName, QDir(config->worldDirectory).dirName())
            .arg(config->maxPlayers));
    if (network.tls.isNull()) {
        say(u"Connections are NOT encrypted: this build has no TLS (QC_TLS=OFF)"_s);
    } else {
        say(u"Certificate fingerprint (SHA-256): %1"_s.arg(QcTlsFingerprint::display(QcTlsFingerprint::of(network.tls.certificate))));
    }
    say(u"Listening on port %1"_s.arg(*port));

    m_dataDirectory = options.dataDirectory;
    startDiagnostics(*config, options.healthSeconds);
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
        const QcResult<QJsonObject> worldOptions = QcHeadlessOptions::worldOptionsOf(settings.worldOptions, settings.worldType, *m_content);
        if (!worldOptions) {
            return qcError(u"Invalid world options: "_s + worldOptions.error());
        }
        const qint64 seed = settings.seed.value_or(static_cast<qint64>(QRandomGenerator::global()->generate64() >> 1));
        const QcWorldLibrary::NewWorld setup{.generator = settings.worldType,
                                             .options = *stored,
                                             .worldOptions = *worldOptions,
                                             .gameMode = settings.defaultGameMode.value_or(QcGameMode::Survival)};
        const QcResult<QcWorldLibrary::Entry> created = library.create(settings.world, seed, setup);
        if (!created) {
            return qcError(created.error());
        }
        say(u"Created the world \"%1\" (seed %2)"_s.arg(created->info.name).arg(created->info.seed));
        entry = *created;
    }

    QcServerConfig config = settings.serverConfig();
    config.worldName = entry.info.name;
    config.seed = entry.info.seed;
    config.generator = entry.info.generator;
    config.generatorOptions = entry.info.generatorOptions;
    config.worldOptions = entry.info.worldOptions;
    config.worldDirectory = entry.directory;
    config.contentPacks = m_packs;
    return config;
}

void QcDedicatedServer::startConsole(bool stopAtEndOfInput)
{
    auto *console = new QcServerConsole(
        [this](const QString &line) {
            // Not a server command: the bundle is about this process and its data directory.
            return line.trimmed() == u"diagnostics"_s ? writeDiagnostics() : m_host->runConsoleCommand(line);
        },
        stopAtEndOfInput, this);
    connect(console, &QcServerConsole::finished, this, [this] { stop(u"end of input"_s, 0); });
    console->start();
}

void QcDedicatedServer::startDiagnostics(const QcServerConfig &config, int healthSeconds)
{
    QStringList mods;
    for (const QcPackFingerprint &pack : std::as_const(m_packs)) {
        mods.append(u"%1 %2"_s.arg(pack.id, pack.version));
    }
    m_worldInfo = {u"World: %1 (folder %2), seed %3, type %4"_s.arg(config.worldName, QDir(config.worldDirectory).dirName())
                       .arg(config.seed)
                       .arg(config.generator),
                   u"Content packs: "_s + mods.join(u", "_s),
                   u"Server: \"%1\", up to %2 players, view distance %3, port %4, password %5"_s.arg(config.serverName)
                       .arg(config.maxPlayers)
                       .arg(config.viewDistance)
                       .arg(m_host->listeningPort())
                       .arg(config.password.isEmpty() ? u"none"_s : u"set"_s)};
    for (const QString &line : std::as_const(m_worldInfo)) {
        qCInfo(lcDiagnostics).noquote() << line;
    }

    if (healthSeconds > 0) {
        new QcHealthLog(healthSeconds, [this] { return m_host ? m_host->health() : QcHealthSample{}; }, this);
    }
}

QStringList QcDedicatedServer::writeDiagnostics() const
{
    const QcResult<QString> written = QcDiagnosticsBundle::write({u"qtcraft-headless"_s, m_dataDirectory, m_worldInfo});
    return {written ? u"Diagnostics written to %1"_s.arg(QDir::toNativeSeparators(*written)) : u"Cannot write the diagnostics: "_s + written.error()};
}

void QcDedicatedServer::stop(const QString &why, int code)
{
    say(u"Stopping (%1)"_s.arg(why));
    QTimer::singleShot(0, qApp, [code] { QCoreApplication::exit(code); });
}
