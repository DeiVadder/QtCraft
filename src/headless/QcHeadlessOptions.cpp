#include "headless/QcHeadlessOptions.h"

#include "common/core/QcDataRoot.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/generators/QcWorldOptions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <print>

using namespace Qt::StringLiterals;

namespace QcHeadlessOptions {

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

std::optional<RunConfig> readConfig(const QCommandLineParser &parser, const Options &options)
{
    RunConfig run;
    QcServerConfig &config = run.server;
    bool seedOk = false;
    config.seed = parser.value(options.seed).toLongLong(&seedOk);
    config.worldName = parser.value(options.world);
    config.generator = parser.value(options.generator);
    config.generatorOptions = parser.value(options.generatorOptions);
    bool viewOk = false;
    config.viewDistance = std::clamp(parser.value(options.view).toInt(&viewOk), 1, 32);
    bool timeOk = false;
    run.timeLimit = parser.value(options.time).toInt(&timeOk);

    bool modeOk = true;
    if (parser.isSet(options.gameMode)) {
        config.defaultGameMode = QcGameModes::parse(parser.value(options.gameMode));
        modeOk = config.defaultGameMode.has_value();
    }

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
    const bool valid = seedOk && viewOk && timeOk && run.timeLimit > 0 && spawnOk && modeOk && QcWorldGenerators::isKnown(config.generator);
    return valid ? std::optional<RunConfig>(std::move(run)) : std::nullopt;
}

QcResult<QJsonObject> worldOptionsOf(const QString &text, const QString &type, const QcContentRegistry &content)
{
    if (text.isEmpty()) {
        return QJsonObject{};
    }
    const QcResult<QJsonObject> given = QcWorldOptions::parseText(text, content);
    if (!given) {
        return qcError(given.error());
    }
    return QcWorldOptions::valuesToStore(type, *given, content);
}

QString dataDirectory(const QCommandLineParser &parser, const Options &options)
{
    return parser.isSet(options.data) ? parser.value(options.data) : QcDataRoot::path();
}

[[nodiscard]] std::optional<QcModLoader::Result> loadContent(const QCommandLineParser &parser, const Options &options)
{
    QString modsDirectory = parser.value(options.mods);
    if (modsDirectory.isEmpty()) {
        modsDirectory = parser.isSet(options.data) ? QDir(parser.value(options.data)).filePath(u"mods"_s) : QcModLoader::defaultDirectory();
    }
    if (parser.isSet(options.noMods)) {
        modsDirectory.clear();
    }

    QcModLoader::Loaded content = QcModLoader::load(modsDirectory);
    for (const QString &warning : QcModLoader::warningsOf(content)) {
        printError(u"content warning: "_s + warning);
    }
    if (!content) {
        printError(u"Cannot load content: "_s + content.error().error);
        return std::nullopt;
    }
    return std::move(*content);
}

namespace {

// `settings` (the file's values) with the options given on the command line; none on a bad number.
[[nodiscard]] std::optional<QcServerSettings> withServerOptions(const QCommandLineParser &parser, const Options &options,
                                                                QcServerSettings settings)
{
    bool ok = true;
    const auto number = [&](const QCommandLineOption &option, int low, int high, int fallback) {
        if (!parser.isSet(option)) {
            return fallback;
        }
        bool valid = false;
        const int value = parser.value(option).toInt(&valid);
        ok = ok && valid && value >= low && value <= high;
        return value;
    };

    settings.port = static_cast<quint16>(number(options.port, 0, 65535, settings.port));
    settings.maxPlayers = number(options.maxPlayers, 1, QcServerSettings::maxPlayersLimit, settings.maxPlayers);
    settings.viewDistance = number(options.view, 1, QcServerSettings::maxViewDistance, settings.viewDistance);
    const auto text = [&parser](const QCommandLineOption &option, const QString &fallback) {
        return parser.isSet(option) ? parser.value(option) : fallback;
    };
    settings.bindAddress = text(options.bind, settings.bindAddress);
    settings.password = text(options.password, settings.password);
    settings.motd = text(options.motd, settings.motd);
    settings.world = text(options.world, settings.world);
    settings.worldType = text(options.generator, settings.worldType);
    settings.worldOptions = text(options.worldOptions, settings.worldOptions);
    if (parser.isSet(options.gameMode)) {
        settings.defaultGameMode = QcGameModes::parse(parser.value(options.gameMode));
        ok = ok && settings.defaultGameMode.has_value();
    }
    // Its own flag: an earlier invalid number must not be overwritten by a valid seed.
    bool seedValid = true;
    if (parser.isSet(options.seed)) {
        settings.seed = parser.value(options.seed).toLongLong(&seedValid);
    }
    if (!ok || !seedValid) {
        return std::nullopt;
    }
    return settings;
}

} // namespace

[[nodiscard]] std::optional<QcServerSettings> serverSettings(const QCommandLineParser &parser, const Options &options)
{
    const QString defaultPath = QDir(dataDirectory(parser, options)).filePath(QcServerSettings::fileName.toString());
    const QString path = parser.isSet(options.serverConfig) ? parser.value(options.serverConfig) : defaultPath;
    QcResult<QcServerSettings> settings = QcServerSettings::read(path);
    if (!settings) {
        printError(settings.error());
        return std::nullopt;
    }
    for (const QString &warning : std::as_const(settings->limitWarnings)) {
        printError(warning);
    }

    // A first start leaves a file with every setting for the operator to edit.
    if (path == defaultPath && !QFile::exists(path)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (const QcStatus written = settings->write(path); !written) {
            printError(written.error());
        }
    }

    if (const QString password = qEnvironmentVariable("QTCRAFT_SERVER_PASSWORD"); !password.isEmpty()) {
        settings->password = password;
    }
    std::optional<QcServerSettings> given = withServerOptions(parser, options, std::move(*settings));
    if (!given) {
        printError(u"Invalid number in the server options"_s);
        return std::nullopt;
    }
    if (const QcResult<QHostAddress> address = given->address(); !address) {
        printError(address.error());
        return std::nullopt;
    }
    return given;
}

} // namespace QcHeadlessOptions
