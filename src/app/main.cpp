#include "QcBundledMods.h"
#include "QcGameController.h"
#include "QcModLoader.h"
#include "QcItemIconProvider.h"
#include "QcScreenChoice.h"
#include "QcScreenshotHook.h"
#include "QcScriptedRun.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QTranslator>
#if defined(QT_QML_DEBUG)
// Profiling builds (QC_QML_DEBUG): the header's static enabler lets qmlprofiler connect.
#include <QtQml/qqmldebug.h>
#endif

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
    const QCommandLineOption cameraOption(u"camera"_s, u"View mode: first, behind or front (test hook, not saved)."_s, u"mode"_s);
    const QCommandLineOption poseOption(u"pose"_s, u"Freeze the player's pose: walk, swing, swim, jump, sneak or death (test hook)."_s,
                                        u"pose"_s);
    const QCommandLineOption lookOption(u"look"_s, u"Initial view direction 'yaw,pitch' in degrees."_s, u"yaw,pitch"_s);
    const QCommandLineOption spawnOption(u"spawn"_s, u"Spawn above the block column 'x,z' (test hook)."_s, u"x,z"_s);
    const QCommandLineOption chatOption(u"chat"_s, u"Chat line or /command sent once the world is loaded (repeatable); '@<ms> <text>' sends it that many milliseconds later."_s,
                                        u"text"_s);
    const QCommandLineOption overlayOption(u"overlay"_s, u"Screen opened once the world is loaded: inventory, pause, settings, character, chat, handbook, furnace (uses the block in view and fills it from hotbar slots 1 and 2) or chest (uses the block in view and moves the hotbar into it)."_s,
                                           u"name"_s);
    const QCommandLineOption handbookOption(u"handbook"_s, u"Open the handbook at an entry ('block:qtcraft:stone', 'zombie') or with a search, once the world is loaded (at once without --play)."_s,
                                            u"entry"_s);
    const QCommandLineOption viewOption(u"view-distance"_s, u"View distance in chunks."_s, u"chunks"_s);
    const QCommandLineOption languageOption(u"language"_s, u"UI language, e.g. 'de'."_s, u"locale"_s);
    const QCommandLineOption modsOption(u"mods-dir"_s, u"Directory with mods (default: the game's mods directory)."_s, u"dir"_s);
    const QCommandLineOption noModsOption(u"no-mods"_s, u"Load only the base game."_s);
    const QCommandLineOption noAudioOption(u"no-audio"_s, u"Do not open an audio device; the game is silent."_s);
    const QCommandLineOption screenOption(u"screen"_s, u"Open the window on this screen: 'builtin' (the computer's own display), an index or a name (also QTCRAFT_SCREEN)."_s,
                                          u"screen"_s);
    const QCommandLineOption backgroundOption(u"background"_s, u"Scripted run: never take the focus or capture the mouse (also QTCRAFT_BACKGROUND=1)."_s);
    parser.addOptions({playOption, worldOption, seedOption, lookOption, cameraOption, poseOption, spawnOption, chatOption, overlayOption, handbookOption, viewOption, languageOption,
                       modsOption, noModsOption, noAudioOption, screenOption, backgroundOption});
    parser.process(app);
    const bool background = parser.isSet(backgroundOption) || qEnvironmentVariable("QTCRAFT_BACKGROUND") == u"1"_s;
    if (background) {
        QcScriptedRun::stayInBackground();
    }

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
            qInfo().noquote() << u"UI language %1: translation loaded"_s.arg(locale.name());
        } else {
            qInfo().noquote() << u"UI language %1: no translation, English texts"_s.arg(locale.name());
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
    if (background) {
        window->setFlag(Qt::WindowDoesNotAcceptFocus, true);
        game->setBackgroundRun(true);
    }
    // Scripted runs open on a fixed screen (the built-in display), not where the mouse happens to be.
    const QString screenRequest = parser.isSet(screenOption) ? parser.value(screenOption) : qEnvironmentVariable("QTCRAFT_SCREEN");
    const QList<QScreen *> screens = QGuiApplication::screens();
    QList<QcScreenChoice::Screen> descriptions;
    const QList<QRect> builtIn = QcScriptedRun::builtInDisplays();
    for (const QScreen *screen : screens) {
        descriptions.append({screen->name(), screen->geometry(), builtIn.contains(screen->geometry()), screen == QGuiApplication::primaryScreen()});
    }
    if (const std::optional<QcScreenChoice::Result> chosen = QcScreenChoice::choose(descriptions, screenRequest)) {
        QScreen *screen = screens[chosen->index];
        window->setScreen(screen);
        window->setGeometry(QcScreenChoice::placeInside(screen->availableGeometry(), window->size()));
        qInfo().noquote() << u"Window on screen %1 \"%2\" (%3)"_s.arg(chosen->index).arg(screen->name(), chosen->reason);
    }
    window->show();
    game->setIconProvider(icons);
    game->loadSettings();
    game->setLanguage(contentLanguage);
    QObject::connect(game, &QcGameController::languageChangeRequested, &engine,
                     [&engine, game, installLanguage](const QString &name) {
                         game->setLanguage(installLanguage(name));
                         engine.retranslate();
                     });
    game->refreshWorlds();
    if (!parser.isSet(noModsOption) && parser.value(modsOption).isEmpty()) {
        // First start of a release bundle: its example mods go, switched off, into the player's
        // mods directory.
        const QString bundled = QcBundledMods::directory(QCoreApplication::applicationDirPath());
        if (const QcResult<QStringList> installed = QcBundledMods::installDisabled(bundled, QcModLoader::defaultDirectory()); !installed) {
            qWarning().noquote() << u"Cannot install the bundled mods:"_s << installed.error();
        } else if (!installed->isEmpty()) {
            qInfo().noquote() << u"Installed bundled mods (switched off) in %1: %2"_s.arg(QcModLoader::defaultDirectory(), installed->join(u", "_s));
        }
    }
    game->setModsDirectory(parser.isSet(noModsOption)          ? QString()
                           : parser.value(modsOption).isEmpty() ? QcModLoader::defaultDirectory()
                                                                : parser.value(modsOption));
    if (parser.isSet(lookOption)) {
        const QStringList parts = parser.value(lookOption).split(u',');
        if (parts.size() == 2) {
            game->setInitialLook(parts[0].toFloat(), parts[1].toFloat());
        }
    }
    if (parser.isSet(cameraOption)) {
        game->setCameraMode(parser.value(cameraOption));
    }
    game->setForcedPose(parser.value(poseOption));
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
    // Screenshot runs (tests, verification) stay silent unless QTCRAFT_AUDIO=1 asks for sound;
    // QTCRAFT_AUDIO=0 or --no-audio silence any run.
    const QString audioSetting = qEnvironmentVariable("QTCRAFT_AUDIO");
    const bool audio = !parser.isSet(noAudioOption) && audioSetting != u"0"_s && (screenshot == nullptr || audioSetting == u"1"_s);
    game->startAudio(audio);
    game->setStartupScript(parser.values(chatOption), parser.value(overlayOption));
    game->setStartupHandbook(parser.value(handbookOption));
    // QTCRAFT_BENCHMARK=<seconds>: measure frame times while walking, then quit (needs --play).
    // QTCRAFT_BENCHMARK_SCENARIO=sprint|walk|stand|light|liquids: sprint ahead (default), walk ahead,
    // stand still, or stand while torches or liquid sources come and go around the player.
    game->setBenchmark(std::clamp(qEnvironmentVariableIntValue("QTCRAFT_BENCHMARK"), 0, 600),
                       qEnvironmentVariable("QTCRAFT_BENCHMARK_SCENARIO"));
    if (parser.isSet(playOption)) {
        game->setScreenshotHook(screenshot);
        game->startSingleplayer(parser.value(worldOption), parser.value(seedOption));
    } else if (parser.isSet(handbookOption) || parser.value(overlayOption) == u"handbook"_s) {
        game->openHandbook(parser.value(handbookOption));
    }

    return QGuiApplication::exec();
}
