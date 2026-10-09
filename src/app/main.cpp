#include "QcBenchmarkRun.h"
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
#include <QStyleHints>
#include <QTranslator>
#if defined(QT_QML_DEBUG)
// Profiling builds (QC_QML_DEBUG): the header's static enabler lets qmlprofiler connect.
#include <QtQml/qqmldebug.h>
#endif

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

// The command line, with the environment variables that stand in for some of its options.
struct AppOptions
{
    bool play = false;
    QString world;
    QString seed;
    std::optional<QString> camera;
    QString pose;
    std::optional<QString> look;
    std::optional<QString> spawn;
    QStringList chat;
    QStringList overlays;
    std::optional<QString> handbook;
    std::optional<QString> viewDistance;
    QString language;
    QString modsDirectory;
    bool noMods = false;
    bool noAudio = false;
    // --screen or QTCRAFT_SCREEN.
    QString screen;
    // --background or QTCRAFT_BACKGROUND=1.
    bool background = false;
};

QList<QCommandLineOption> commandLineOptions()
{
    return {
        {u"play"_s, u"Start a single-player world immediately."_s},
        {u"world"_s, u"World name for --play."_s, u"name"_s, u"Test World"_s},
        {u"seed"_s, u"World seed for --play."_s, u"seed"_s, u"0"_s},
        {u"look"_s, u"Initial view direction 'yaw,pitch' in degrees."_s, u"yaw,pitch"_s},
        {u"camera"_s, u"View mode: first, behind or front (test hook, not saved)."_s, u"mode"_s},
        {u"pose"_s, u"Freeze the player's pose: walk, swing, swim, jump, sneak or death (test hook)."_s, u"pose"_s},
        {u"spawn"_s, u"Spawn above the block column 'x,z' (test hook)."_s, u"x,z"_s},
        {u"chat"_s, u"Chat line or /command sent once the world is loaded (repeatable); '@<ms> <text>' sends it that many milliseconds later."_s,
         u"text"_s},
        {u"overlay"_s, u"Screen opened once the world is loaded: inventory, pause, settings, controls, character, chat, handbook, furnace (uses the block in view and fills it from hotbar slots 1 and 2) or chest (uses the block in view and moves the hotbar into it). Repeatable: '@<ms> <screen>' switches that many milliseconds later ('@<ms> ' closes the screen)."_s,
         u"name"_s},
        {u"handbook"_s, u"Open the handbook at an entry ('block:qtcraft:stone', 'zombie') or with a search, once the world is loaded (at once without --play)."_s,
         u"entry"_s},
        {u"view-distance"_s, u"View distance in chunks."_s, u"chunks"_s},
        {u"language"_s, u"UI language, e.g. 'de'."_s, u"locale"_s},
        {u"mods-dir"_s, u"Directory with mods (default: the game's mods directory)."_s, u"dir"_s},
        {u"no-mods"_s, u"Load only the base game."_s},
        {u"no-audio"_s, u"Do not open an audio device; the game is silent."_s},
        {u"screen"_s, u"Open the window on this screen: 'builtin' (the computer's own display), an index or a name (also QTCRAFT_SCREEN)."_s,
         u"screen"_s},
        {u"background"_s, u"Scripted run: never take the focus or capture the mouse (also QTCRAFT_BACKGROUND=1)."_s},
    };
}

// Handles --help and --version itself (and quits).
AppOptions parseOptions(const QGuiApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"QtCraft voxel sandbox"_s);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions(commandLineOptions());
    parser.process(app);

    const auto optional = [&parser](const QString &name) { return parser.isSet(name) ? std::optional(parser.value(name)) : std::nullopt; };
    AppOptions options;
    options.play = parser.isSet(u"play"_s);
    options.world = parser.value(u"world"_s);
    options.seed = parser.value(u"seed"_s);
    options.camera = optional(u"camera"_s);
    options.pose = parser.value(u"pose"_s);
    options.look = optional(u"look"_s);
    options.spawn = optional(u"spawn"_s);
    options.chat = parser.values(u"chat"_s);
    options.overlays = parser.values(u"overlay"_s);
    options.handbook = optional(u"handbook"_s);
    options.viewDistance = optional(u"view-distance"_s);
    options.language = parser.value(u"language"_s);
    options.modsDirectory = parser.value(u"mods-dir"_s);
    options.noMods = parser.isSet(u"no-mods"_s);
    options.noAudio = parser.isSet(u"no-audio"_s);
    options.screen = parser.isSet(u"screen"_s) ? parser.value(u"screen"_s) : qEnvironmentVariable("QTCRAFT_SCREEN");
    options.background = parser.isSet(u"background"_s) || qEnvironmentVariable("QTCRAFT_BACKGROUND") == u"1"_s;
    return options;
}

// --language or QTCRAFT_LANGUAGE override the language chosen in the settings, which overrides
// the system language.
QString uiLanguage(const AppOptions &options)
{
    QString language = options.language;
    if (language.isEmpty()) {
        language = qEnvironmentVariable("QTCRAFT_LANGUAGE");
    }
    if (language.isEmpty()) {
        language = QcSettingsController::storedLanguage();
    }
    return language;
}

// The game screens are drawn dark over the world (E-079), so the controls are dark too, whatever
// the system appearance. QTCRAFT_COLOR_SCHEME=light or =system is a test hook for comparison.
void applyColorScheme()
{
    const QString scheme = qEnvironmentVariable("QTCRAFT_COLOR_SCHEME");
    if (scheme != u"system"_s) {
        QGuiApplication::styleHints()->setColorScheme(scheme == u"light"_s ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    }
}

// Installs the UI translation for `name` ("" = system); returns the language for content names.
QString installLanguage(QTranslator &translator, const QString &name)
{
    const QLocale locale = name.isEmpty() ? QLocale::system() : QLocale(name);
    QCoreApplication::removeTranslator(&translator);
    if (translator.load(locale, u"qtcraft"_s, u"_"_s, u":/i18n"_s)) {
        QCoreApplication::installTranslator(&translator);
        qInfo().noquote() << u"UI language %1: translation loaded"_s.arg(locale.name());
    } else {
        qInfo().noquote() << u"UI language %1: no translation, English texts"_s.arg(locale.name());
    }
    return locale.name().left(2);
}

// Scripted runs open on a fixed screen (the built-in display), not where the mouse happens to be.
void placeWindow(QQuickWindow *window, const QString &screenRequest)
{
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
}

// First start of a release bundle: its example mods go, switched off, into the player's mods
// directory.
void installBundledMods()
{
    const QString bundled = QcBundledMods::directory(QCoreApplication::applicationDirPath());
    if (const QcResult<QStringList> installed = QcBundledMods::installDisabled(bundled, QcModLoader::defaultDirectory()); !installed) {
        qWarning().noquote() << u"Cannot install the bundled mods:"_s << installed.error();
    } else if (!installed->isEmpty()) {
        qInfo().noquote() << u"Installed bundled mods (switched off) in %1: %2"_s.arg(QcModLoader::defaultDirectory(), installed->join(u", "_s));
    }
}

void setUpMods(QcGameController &game, const AppOptions &options)
{
    if (!options.noMods && options.modsDirectory.isEmpty()) {
        installBundledMods();
    }
    game.library()->setModsDirectory(options.noMods                  ? QString()
                                     : options.modsDirectory.isEmpty() ? QcModLoader::defaultDirectory()
                                                                       : options.modsDirectory);
}

// Test hooks for reproducible scenes: view direction, camera, pose, spawn column, view distance.
void applySceneOptions(QcGameController &game, const AppOptions &options)
{
    if (options.look) {
        const QStringList parts = options.look->split(u',');
        if (parts.size() == 2) {
            game.startup()->setInitialLook(parts[0].toFloat(), parts[1].toFloat());
        }
    }
    if (options.camera) {
        game.settings()->setCameraMode(*options.camera);
    }
    game.startup()->setForcedPose(options.pose);
    if (options.spawn) {
        const QStringList parts = options.spawn->split(u',');
        if (parts.size() == 2) {
            game.startup()->setSpawnColumn(parts[0].toInt(), parts[1].toInt());
        }
    }
    if (options.viewDistance) {
        game.settings()->overrideViewDistance(options.viewDistance->toInt());
    }
}

// Audio, the startup script, the benchmark and the world or handbook to open at once.
void startScriptedRun(QcGameController &game, const AppOptions &options, QcScreenshotHook *screenshot)
{
    // Screenshot runs (tests, verification) stay silent unless QTCRAFT_AUDIO=1 asks for sound;
    // QTCRAFT_AUDIO=0 or --no-audio silence any run.
    const QString audioSetting = qEnvironmentVariable("QTCRAFT_AUDIO");
    const bool audio = !options.noAudio && audioSetting != u"0"_s && (screenshot == nullptr || audioSetting == u"1"_s);
    game.startAudio(audio);
    game.startup()->setStartupScript(options.chat, options.overlays);
    game.startup()->setStartupHandbook(options.handbook.value_or(QString()));
    // QTCRAFT_BENCHMARK=<seconds>: measure frame times while walking, then quit (needs --play).
    // QTCRAFT_BENCHMARK_SCENARIO=sprint|walk|stand|light|liquids: sprint ahead (default), walk ahead,
    // stand still, or stand while torches or liquid sources come and go around the player.
    game.startup()->benchmark()->configure(std::clamp(qEnvironmentVariableIntValue("QTCRAFT_BENCHMARK"), 0, 600),
                                           qEnvironmentVariable("QTCRAFT_BENCHMARK_SCENARIO"));

    if (options.play) {
        game.startup()->setScreenshotHook(screenshot);
        game.startSingleplayer(options.world, options.seed);
    } else if (options.handbook || options.overlays.contains(u"handbook"_s)) {
        game.handbook()->open(options.handbook.value_or(QString()));
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"QtCraft"_s);
    QGuiApplication::setOrganizationName(u"QtCraft"_s);
    QGuiApplication::setApplicationVersion(QStringLiteral(QTCRAFT_VERSION));
    const AppOptions options = parseOptions(app);
    if (options.background) {
        QcScriptedRun::stayInBackground();
    }

    const QString language = uiLanguage(options);
    applyColorScheme();
    QTranslator translator;
    const QString contentLanguage = installLanguage(translator, language);

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
    if (options.background) {
        window->setFlag(Qt::WindowDoesNotAcceptFocus, true);
        game->input()->setBackgroundRun(true);
    }
    placeWindow(window, options.screen);
    window->show();

    game->setIconProvider(icons);
    game->settings()->loadSettings();
    game->setLanguage(contentLanguage);
    QObject::connect(game->settings(), &QcSettingsController::languageChangeRequested, &engine,
                     [&engine, &translator, game](const QString &name) {
                         game->setLanguage(installLanguage(translator, name));
                         engine.retranslate();
                     });
    game->library()->refreshWorlds();
    setUpMods(*game, options);
    applySceneOptions(*game, options);
    QcScreenshotHook *screenshot = QcScreenshotHook::createFromEnvironment(window, &app);
    startScriptedRun(*game, options, screenshot);

    return QGuiApplication::exec();
}
