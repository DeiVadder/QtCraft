#include "QcGameController.h"
#include "QcModLoader.h"
#include "QcItemIconProvider.h"
#include "QcScreenshotHook.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTranslator>

#include <algorithm>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"QtCraft"_s);
    QGuiApplication::setOrganizationName(u"QtCraft"_s);
    QGuiApplication::setApplicationVersion(QStringLiteral(QTCRAFT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(u"QtCraft voxel sandbox"_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption playOption(u"play"_s, u"Start a single-player world immediately."_s);
    const QCommandLineOption worldOption(u"world"_s, u"World name for --play."_s, u"name"_s, u"Test World"_s);
    const QCommandLineOption seedOption(u"seed"_s, u"World seed for --play."_s, u"seed"_s, u"0"_s);
    const QCommandLineOption lookOption(u"look"_s, u"Initial view direction 'yaw,pitch' in degrees."_s, u"yaw,pitch"_s);
    const QCommandLineOption spawnOption(u"spawn"_s, u"Spawn above the block column 'x,z' (test hook)."_s, u"x,z"_s);
    const QCommandLineOption chatOption(u"chat"_s, u"Chat line or /command sent once the world is loaded (repeatable)."_s,
                                        u"text"_s);
    const QCommandLineOption overlayOption(u"overlay"_s, u"Screen opened once the world is loaded: inventory, pause, settings, chat."_s,
                                           u"name"_s);
    const QCommandLineOption viewOption(u"view-distance"_s, u"View distance in chunks."_s, u"chunks"_s);
    const QCommandLineOption languageOption(u"language"_s, u"UI language, e.g. 'de'."_s, u"locale"_s);
    const QCommandLineOption modsOption(u"mods-dir"_s, u"Directory with mods (default: the game's mods directory)."_s, u"dir"_s);
    const QCommandLineOption noModsOption(u"no-mods"_s, u"Load only the base game."_s);
    parser.addOptions({playOption, worldOption, seedOption, lookOption, spawnOption, chatOption, overlayOption, viewOption, languageOption,
                       modsOption, noModsOption});
    parser.process(app);

    // --language or QTCRAFT_LANGUAGE override the language chosen in the settings, which overrides
    // the system language.
    QString language = parser.value(languageOption);
    if (language.isEmpty()) {
        language = qEnvironmentVariable("QTCRAFT_LANGUAGE");
    }
    if (language.isEmpty()) {
        language = QcGameController::storedLanguage();
    }
    QTranslator translator;
    const auto installLanguage = [&translator](const QString &name) {
        const QLocale locale = name.isEmpty() ? QLocale::system() : QLocale(name);
        QCoreApplication::removeTranslator(&translator);
        if (translator.load(locale, u"qtcraft"_s, u"_"_s, u":/i18n"_s)) {
            QCoreApplication::installTranslator(&translator);
        }
        return locale.name().left(2);
    };
    const QString contentLanguage = installLanguage(language);

    QQmlApplicationEngine engine;
    // The engine owns and deletes the provider.
    auto *icons = new QcItemIconProvider;
    engine.addImageProvider(QString::fromLatin1(QcItemIconProvider::providerId), icons);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("QtCraft", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    auto *game = window ? window->findChild<QcGameController *>(u"game"_s) : nullptr;
    if (!game) {
        qCritical("Main.qml must contain a GameController with objectName 'game'");
        return 1;
    }
    game->setIconProvider(icons);
    game->loadSettings();
    game->setLanguage(contentLanguage);
    QObject::connect(game, &QcGameController::languageChangeRequested, &engine,
                     [&engine, game, installLanguage](const QString &name) {
                         game->setLanguage(installLanguage(name));
                         engine.retranslate();
                     });
    game->refreshWorlds();
    game->setModsDirectory(parser.isSet(noModsOption)          ? QString()
                           : parser.value(modsOption).isEmpty() ? QcModLoader::defaultDirectory()
                                                                : parser.value(modsOption));
    if (parser.isSet(lookOption)) {
        const QStringList parts = parser.value(lookOption).split(u',');
        if (parts.size() == 2) {
            game->setInitialLook(parts[0].toFloat(), parts[1].toFloat());
        }
    }
    if (parser.isSet(spawnOption)) {
        const QStringList parts = parser.value(spawnOption).split(u',');
        if (parts.size() == 2) {
            game->setSpawnColumn(parts[0].toInt(), parts[1].toInt());
        }
    }
    if (parser.isSet(viewOption)) {
        game->overrideViewDistance(parser.value(viewOption).toInt());
    }
    QcScreenshotHook *screenshot = QcScreenshotHook::createFromEnvironment(window, &app);
    game->setStartupScript(parser.values(chatOption), parser.value(overlayOption));
    // QTCRAFT_BENCHMARK=<seconds>: measure frame times while walking, then quit (needs --play).
    game->setBenchmark(std::clamp(qEnvironmentVariableIntValue("QTCRAFT_BENCHMARK"), 0, 600));
    if (parser.isSet(playOption)) {
        game->setScreenshotHook(screenshot);
        game->startSingleplayer(parser.value(worldOption), parser.value(seedOption));
    }

    return QGuiApplication::exec();
}
