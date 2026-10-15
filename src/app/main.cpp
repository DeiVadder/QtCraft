#include "QcBenchmarkRun.h"
#include "QcBundledMods.h"
#include "QcControlPalette.h"
#include "QcGameController.h"
#include "QcGameMode.h"
#include "QcModLoader.h"
#include "QcOffscreenRender.h"
#include "QcItemIconProvider.h"
#include "QcTexturePackPreviewProvider.h"
#include "QcScreenChoice.h"
#include "QcScreenshotHook.h"
#include "QcScriptedRun.h"
#include "QcStatsLog.h"
#include "QcWorldGenerators.h"

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
#include <memory>
#include <optional>
#include <print>

using namespace Qt::StringLiterals;

namespace {

// The command line, with the environment variables that stand in for some of its options.
struct AppOptions
{
    bool play = false;
    // --world-type (or --showcase) and --flat-layers: the type of the world --play starts.
    QString worldType;
    QString flatLayers;
    // --game-mode: the player's mode in the world --play starts.
    QString gameMode;
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
    // --stats (or QTCRAFT_STATS), --debug-display and --debug-show (decision E-225).
    QString stats;
    std::optional<QString> debugDisplay;
    std::optional<QString> debugShow;
    // --preview: what every character preview shows (decision E-231).
    QcPreviewHook preview;
    // --join, --player-name, --password (or QTCRAFT_SERVER_PASSWORD) and --host-lan (decision E-251).
    std::optional<QString> join;
    QString playerName;
    QString password;
    std::optional<int> hostLan;
    // What is wrong with the command line beyond what QCommandLineParser checks.
    QString error;
};

QList<QCommandLineOption> commandLineOptions()
{
    return {
        {u"play"_s, u"Start a single-player world immediately."_s},
        {u"world-type"_s, u"World type for --play: terrain (default), flat, void or showcase (implies --play)."_s, u"type"_s},
        {u"flat-layers"_s, u"Layers of a flat world for --play, bottom up, e.g. 'bedrock,59*stone,3*dirt,grass_block' (with --world-type flat)."_s,
         u"layers"_s},
        {u"game-mode"_s, u"Game mode for --play: survival (default) or creative."_s, u"mode"_s, u"survival"_s},
        {u"showcase"_s, u"Start an unsaved showcase world immediately: every block, creature and item on show (same as --world-type showcase)."_s},
        {u"world"_s, u"World name for --play."_s, u"name"_s, u"Test World"_s},
        {u"seed"_s, u"World seed for --play."_s, u"seed"_s, u"0"_s},
        {u"look"_s, u"Initial view direction 'yaw,pitch' in degrees."_s, u"yaw,pitch"_s},
        {u"camera"_s, u"View mode: first, behind, front or side (test hook, not saved)."_s, u"mode"_s},
        {u"pose"_s, u"Freeze the player's pose: walk, swing (swing:<0..1> for a moment of the swing), swim, jump, sneak or death (test hook)."_s, u"pose"_s},
        {u"spawn"_s, u"Spawn above the block column 'x,z' (test hook)."_s, u"x,z"_s},
        {u"chat"_s, u"Chat line or /command sent once the world is loaded (repeatable); '@<ms> <text>' sends it that many milliseconds later."_s,
         u"text"_s},
        {u"overlay"_s, u"Screen opened once the world is loaded: inventory, pause, settings, controls, debug (the debug options), character, lan (open to LAN), players (the player list as if its key were held), chat, handbook, furnace (uses the block in view and fills it from hotbar slots 1 and 2) or chest (uses the block in view and moves the hotbar into it). Without --play: multiplayer opens the main menu's multiplayer screen (with --join the join runs there and a refusal stays on screen). Repeatable: '@<ms> <screen>' switches that many milliseconds later ('@<ms> ' closes the screen and the handbook, '@<ms> pick:<row>' picks up a stack in the open slot screen, '@<ms> hover:<x>,<y>' moves the pointer there, e.g. for a tooltip, '@<ms> open:<objectName>' opens that combo box's list, popup or dialog, '@<ms> text:<objectName>=<text>' types into a field, '@<ms> fly' starts flying in creative mode)."_s,
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
        {u"stats"_s, u"Log the debug data as a JSON line 'STATS {...}' every this many milliseconds while a world runs (also QTCRAFT_STATS)."_s,
         u"ms"_s},
        {u"debug-display"_s, u"Debug display level: off, compact or full (like the debug key; kept in the settings)."_s, u"level"_s},
        {u"debug-show"_s, u"Debug visualisations to switch on, comma-separated: chunkBorders, hitboxes, lightValues, mobGoals, remeshFlash (kept in the settings)."_s,
         u"list"_s},
        {u"preview"_s, u"Fix what the character previews show (test hook), comma-separated: animation=stand|walk|mine, side=front|back, turn=<degrees>, zoom=<0..1>, gaze=<x>:<y>, time=<seconds>."_s,
         u"settings"_s},
        {u"join"_s, u"Join the network game at 'host[:port]' at once ([v6]:port for IPv6)."_s, u"address"_s},
        {u"player-name"_s, u"Player name for --join."_s, u"name"_s, u"Player"_s},
        {u"password"_s, u"Server password for --join (also QTCRAFT_SERVER_PASSWORD)."_s, u"password"_s},
        {u"host-lan"_s, u"With --play: open the world to network players on this port once it runs (0: any free port)."_s, u"port"_s},
    };
}

// --world-type names a known type, --flat-layers comes with --world-type flat. Whether the layers
// themselves are valid is known once the content is loaded (QcGameController::worldRefused).
QString worldTypeError(const AppOptions &options, bool flatLayersGiven)
{
    if (!options.worldType.isEmpty() && !QcWorldGenerators::isKnown(options.worldType)) {
        return u"Unknown world type '%1' (%2)"_s.arg(options.worldType, QcWorldGenerators::types().join(u", "_s));
    }
    if (flatLayersGiven && options.worldType != QLatin1StringView(QcWorldGenerators::flat)) {
        return u"--flat-layers needs --world-type flat"_s;
    }
    return {};
}

// --debug-display names a level, --debug-show only known visualisations.
QString debugOptionsError(const AppOptions &options)
{
    if (options.debugDisplay && !QcDebugOptions::levelFromName(*options.debugDisplay)) {
        return u"Unknown debug display level '%1' (off, compact, full)"_s.arg(*options.debugDisplay);
    }
    const QStringList visuals = options.debugShow.value_or(QString()).split(u',', Qt::SkipEmptyParts);
    for (const QString &visual : visuals) {
        if (!QcDebugOptions::visualFromId(visual.trimmed())) {
            return u"Unknown debug visualisation '%1'"_s.arg(visual.trimmed());
        }
    }
    return {};
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
    options.worldType = parser.isSet(u"showcase"_s) ? u"showcase"_s : parser.value(u"world-type"_s);
    options.flatLayers = parser.value(u"flat-layers"_s);
    options.gameMode = parser.value(u"game-mode"_s);
    options.play = parser.isSet(u"play"_s) || !options.worldType.isEmpty();
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
    options.stats = parser.value(u"stats"_s);
    options.debugDisplay = optional(u"debug-display"_s);
    options.debugShow = optional(u"debug-show"_s);
    options.join = optional(u"join"_s);
    options.playerName = parser.value(u"player-name"_s);
    options.password = parser.isSet(u"password"_s) ? parser.value(u"password"_s) : qEnvironmentVariable("QTCRAFT_SERVER_PASSWORD");
    if (parser.isSet(u"host-lan"_s)) {
        options.hostLan = parser.value(u"host-lan"_s).toInt();
    }
    options.error = worldTypeError(options, parser.isSet(u"flat-layers"_s));
    if (options.error.isEmpty() && !QcGameModes::parse(options.gameMode)) {
        options.error = u"Unknown game mode '%1' (survival or creative)"_s.arg(options.gameMode);
    }
    if (options.error.isEmpty()) {
        options.error = debugOptionsError(options);
    }
    if (const QcResult<QcPreviewHook> preview = QcPreviewHook::parse(parser.value(u"preview"_s)); !preview) {
        options.error = options.error.isEmpty() ? preview.error() : options.error;
    } else {
        options.preview = *preview;
    }
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
// the system appearance. QTCRAFT_COLOR_SCHEME=light or =system is a test hook for comparison;
// =system also keeps the system's control palette instead of the game's (E-279).
bool systemColorScheme()
{
    return qEnvironmentVariable("QTCRAFT_COLOR_SCHEME") == u"system"_s;
}

void applyColorScheme()
{
    if (!systemColorScheme()) {
        const bool light = qEnvironmentVariable("QTCRAFT_COLOR_SCHEME") == u"light"_s;
        QGuiApplication::styleHints()->setColorScheme(light ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark);
    }
}

// Popups, dialogs and tooltips take their colours from their window (E-279).
void applyControlPalette(QQuickWindow &window)
{
    if (!systemColorScheme() && !QcControlPalette::applyTo(window)) {
        qWarning("The window offers no control palette; controls keep the system colours");
    }
}

// QTCRAFT_MENU_WORLD_TYPE=flat|void|showcase is a test hook that preselects the world type of the
// menu's new world, so that screenshots show the fields of that type.
void preselectWorldType(const QQuickWindow &window)
{
    const qsizetype index = QcWorldGenerators::types().indexOf(qEnvironmentVariable("QTCRAFT_MENU_WORLD_TYPE"));
    QObject *choice = window.findChild<QObject *>(u"worldTypeChoice"_s);
    if (index >= 0 && choice) {
        choice->setProperty("currentIndex", int(index));
    }

    // QTCRAFT_MENU_GAME_MODE=creative preselects the game mode the same way.
    QObject *mode = window.findChild<QObject *>(u"gameModeChoice"_s);
    if (mode && QcGameModes::parse(qEnvironmentVariable("QTCRAFT_MENU_GAME_MODE")) == QcGameMode::Creative) {
        mode->setProperty("currentIndex", 1);
    }
}

// Without --play (test hooks): --overlay multiplayer opens the main menu's multiplayer screen; with
// --join it joins from there, so a refusal is shown like after a click. Runs the menu steps of the
// script ("@<ms> open:<objectName>", "@<ms> hover:<x>,<y>").
void openMenuScreen(const QQuickWindow &window, QcGameController &game, const AppOptions &options)
{
    if (options.play) {
        return;
    }
    game.startup()->menuShown();
    if (!options.overlays.contains(u"multiplayer"_s)) {
        return;
    }
    // Popups are no QObject children of the window; the menu opens its own screen.
    QObject *menu = window.findChild<QObject *>(u"mainMenu"_s);
    if (!menu) {
        qWarning("--overlay multiplayer: no main menu");
        return;
    }

    QMetaObject::invokeMethod(menu, "openMultiplayer", Q_ARG(QString, options.join.value_or(QString())), Q_ARG(QString, options.playerName),
                              Q_ARG(QString, options.password));
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

// QTCRAFT_WINDOW_SIZE=<width>x<height> (test hook): screenshots of small windows. The window's
// minimum size still applies.
void applyWindowSize(QQuickWindow &window)
{
    const QStringList size = qEnvironmentVariable("QTCRAFT_WINDOW_SIZE").split(u'x');
    if (size.size() == 2 && size[0].toInt() > 0 && size[1].toInt() > 0) {
        window.resize(QSize(size[0].toInt(), size[1].toInt()).expandedTo(window.minimumSize()));
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
    game.preview()->setHook(options.preview);
    // Benchmarks of the preview's costs: animated although the scripted run's window is inactive.
    game.preview()->setForceActive(qEnvironmentVariable("QTCRAFT_PREVIEW_ACTIVE") == u"1"_s);
    if (options.spawn) {
        const QStringList parts = options.spawn->split(u',');
        if (parts.size() == 2) {
            game.startup()->setSpawnColumn(parts[0].toInt(), parts[1].toInt());
        }
    }
    if (options.viewDistance) {
        game.settings()->overrideViewDistance(options.viewDistance->toInt());
    }
    // Both were checked by debugOptionsError().
    if (const std::optional<QcDebugOptions::Level> level = QcDebugOptions::levelFromName(options.debugDisplay.value_or(QString()))) {
        game.settings()->debug()->setLevel(int(*level));
    }
    const QStringList visuals = options.debugShow.value_or(QString()).split(u',', Qt::SkipEmptyParts);
    for (const QString &name : visuals) {
        if (const std::optional<QcDebugOptions::Visual> visual = QcDebugOptions::visualFromId(name.trimmed())) {
            game.settings()->debug()->setShown(*visual, true);
        }
    }
}

// --join: plays a network game at once; a failure ends the run like a bad option. --host-lan:
// opens the --play world to the network once it runs and prints the port.
void startNetworkRun(QcGameController &game, const AppOptions &options)
{
    QcMultiplayerController *multiplayer = game.multiplayer();
    if (options.join) {
        QObject::connect(
            multiplayer, &QcMultiplayerController::joinFailed, qApp,
            [](const QString &reason) {
                std::println(stderr, "qtcraft: cannot join: {}", reason.toStdString());
                QCoreApplication::exit(1);
            },
            Qt::QueuedConnection);
        multiplayer->join(*options.join, options.playerName, options.password);
        return;
    }

    if (options.hostLan) {
        auto opened = std::make_shared<bool>(false);
        QObject::connect(&game, &QcGameController::phaseChanged, multiplayer, [&game, multiplayer, port = *options.hostLan, opened] {
            if (*opened || game.phase() != QcGameController::Phase::Playing) {
                return;
            }
            *opened = true;
            const int listening = multiplayer->openToLan(port);
            std::println("Open to the network on port {}", listening);
            std::fflush(stdout);
        });
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
    game.startStatsLog(QcStatsLog::interval(options.stats));
    // QTCRAFT_BENCHMARK_PACK_SWITCH=<ids>: switch these texture packs on and off during the run and
    // log the hitch (decision E-332).
    game.startup()->benchmark()->configure(std::clamp(qEnvironmentVariableIntValue("QTCRAFT_BENCHMARK"), 0, 600),
                                           qEnvironmentVariable("QTCRAFT_BENCHMARK_SCENARIO"),
                                           qEnvironmentVariable("QTCRAFT_BENCHMARK_PACK_SWITCH").split(u',', Qt::SkipEmptyParts));

    if (options.play) {
        // Layers that do not fit the content end a run from the command line like a bad option.
        QObject::connect(
            &game, &QcGameController::worldRefused, qApp,
            [](const QString &reason) {
                std::println(stderr, "qtcraft: {}", reason.toStdString());
                QCoreApplication::exit(1);
            },
            Qt::QueuedConnection);
        game.startup()->setScreenshotHook(screenshot);
        startNetworkRun(game, options);
        game.startSingleplayer(options.world, options.seed, options.worldType.isEmpty() ? u"terrain"_s : options.worldType, options.flatLayers,
                               options.gameMode);
    } else if (options.join && options.overlays.contains(u"multiplayer"_s)) {
        // The menu's screen joins (openMenuScreen); the screenshot shows its state, a refusal included.
    } else if (options.join) {
        game.startup()->setScreenshotHook(screenshot);
        startNetworkRun(game, options);
    } else if (options.handbook || options.overlays.contains(u"handbook"_s)) {
        game.handbook()->open(options.handbook.value_or(QString()));
    }
}

// Offscreen runs (E-229) are for screenshots: their frame times say nothing about the real window
// (benchmarks measure that), and without a screenshot to take nothing would ever end them.
QString offscreenRunError()
{
    if (qEnvironmentVariableIntValue("QTCRAFT_BENCHMARK") > 0) {
        return u"Benchmarks measure the real window and do not run offscreen (set QTCRAFT_OFFSCREEN=0, decision E-229)"_s;
    }
    if (qEnvironmentVariableIsEmpty("QTCRAFT_SCREENSHOT")) {
        return u"An offscreen run needs QTCRAFT_SCREENSHOT: without a window nothing would end it"_s;
    }
    return {};
}

// Scripted runs keep out of the user's way; offscreen runs are scripted runs too.
void prepareScriptedRun(const AppOptions &options, bool offscreen)
{
    if (options.background || offscreen) {
        QcScriptedRun::stayInBackground();
    }
    if (offscreen) {
        QcScriptedRun::keepTimersRunning();
        QcOffscreenRender::prepare();
    }
}

// Shows the main window, or moves its content into an offscreen renderer (returned). The error
// when offscreen rendering cannot start.
[[nodiscard]] QcResult<std::unique_ptr<QcOffscreenRender>> startWindowOrOffscreen(QQuickWindow &window, QcGameController &game, const AppOptions &options,
                                                                    bool offscreen)
{
    if (options.background || offscreen) {
        window.setFlag(Qt::WindowDoesNotAcceptFocus, true);
        // Above other windows: macOS stops drawing a window that others cover completely, and a
        // benchmark or screenshot behind the user's windows would get no frames.
        window.setFlag(Qt::WindowStaysOnTopHint, true);
        game.input()->setBackgroundRun(true);
    }
    applyWindowSize(window);
    applyControlPalette(window);
    placeWindow(&window, options.screen);
    preselectWorldType(window);

    if (!offscreen) {
        window.show();
        return nullptr;
    }
    game.preview()->setOffscreen(true);
    // Created after placing: it takes the window's size and screen.
    auto render = std::make_unique<QcOffscreenRender>(window);
    applyControlPalette(*render->window());
    if (!render->start()) {
        return std::unexpected(u"Cannot render offscreen"_s);
    }
    return render;
}

// The parts of the user interface the running game keeps using: the engine and translator switch
// the language, the icon provider draws item icons.
struct UiParts
{
    QQmlApplicationEngine &engine;
    QTranslator &translator;
    QString contentLanguage;
    QcItemIconProvider *icons = nullptr;
    QcTexturePackPreviewProvider *texturePackPreviews = nullptr;
};

// Settings, language, worlds, mods and the scripted parts of the run; screenshots come from the
// offscreen renderer when there is one.
void startGame(const UiParts &ui, QcGameController &game, const AppOptions &options, QQuickWindow &window, QcOffscreenRender *offscreenRender)
{
    game.setIconProvider(ui.icons);
    game.settings()->texturePacks()->setPreviewProvider(ui.texturePackPreviews);
    // Texture packs for this run only, e.g. for screenshots and benchmarks: comma-separated pack
    // ids, highest priority first, or "none" (decision E-304); set before the settings load them.
    if (const QString packs = qEnvironmentVariable("QTCRAFT_TEXTURE_PACKS"); !packs.isEmpty()) {
        game.settings()->texturePacks()->overrideSelection(packs == u"none"_s ? QStringList() : packs.split(u',', Qt::SkipEmptyParts));
    }
    game.settings()->loadSettings();
    game.setLanguage(ui.contentLanguage);
    QObject::connect(game.settings(), &QcSettingsController::languageChangeRequested, &ui.engine,
                     [&engine = ui.engine, &translator = ui.translator, &game](const QString &name) {
                         game.setLanguage(installLanguage(translator, name));
                         engine.retranslate();
                     });
    game.library()->refreshWorlds();
    setUpMods(game, options);
    applySceneOptions(game, options);

    QcScreenshotHook *screenshot = QcScreenshotHook::createFromEnvironment(offscreenRender ? offscreenRender->window() : &window, qApp);
    if (screenshot && offscreenRender) {
        screenshot->setGrabber([offscreenRender] { return offscreenRender->grab(); });
    }
    startScriptedRun(game, options, screenshot);
    openMenuScreen(offscreenRender ? *offscreenRender->window() : window, game, options);
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(u"QtCraft"_s);
    QGuiApplication::setOrganizationName(u"QtCraft"_s);
    QGuiApplication::setApplicationVersion(QStringLiteral(QTCRAFT_VERSION));
    AppOptions options = parseOptions(app);
    const bool offscreen = QcOffscreenRender::requested();
    if (options.error.isEmpty() && offscreen) {
        options.error = offscreenRunError();
    }
    if (!options.error.isEmpty()) {
        std::println(stderr, "qtcraft: {}", options.error.toStdString());
        return 1;
    }
    prepareScriptedRun(options, offscreen);

    const QString language = uiLanguage(options);
    applyColorScheme();
    QTranslator translator;
    const QString contentLanguage = installLanguage(translator, language);

    QQmlApplicationEngine engine;
    // The engine owns and deletes the provider.
    auto *icons = new QcItemIconProvider;
    engine.addImageProvider(QString::fromLatin1(QcItemIconProvider::providerId), icons);
    auto *texturePackPreviews = new QcTexturePackPreviewProvider;
    engine.addImageProvider(QString::fromLatin1(QcTexturePackPreviewProvider::providerId), texturePackPreviews);
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
    const QcResult<std::unique_ptr<QcOffscreenRender>> offscreenRender = startWindowOrOffscreen(*window, *game, options, offscreen);
    if (!offscreenRender) {
        std::println(stderr, "qtcraft: {}", offscreenRender.error().toStdString());
        return 1;
    }

    startGame({engine, translator, contentLanguage, icons, texturePackPreviews}, *game, options, *window, offscreenRender->get());
    return QGuiApplication::exec();
}
