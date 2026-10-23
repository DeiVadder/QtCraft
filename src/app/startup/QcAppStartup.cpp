#include "app/startup/QcAppStartup.h"

#include "app/controllers/QcGameController.h"
#include "app/debug/QcAppLogging.h"
#include "app/hooks/QcBenchmarkRun.h"
#include "app/hooks/QcOffscreenRender.h"
#include "app/hooks/QcScreenshotHook.h"
#include "app/hooks/QcScriptedPad.h"
#include "app/startup/QcAppOptions.h"
#include "clientcore/camera/QcViewCamera.h"
#include "clientcore/debug/QcStatsLog.h"
#include "worlds/QcBundledMods.h"
#include "worlds/QcModLoader.h"

#include <QCoreApplication>
#include <QQuickWindow>

#include <algorithm>
#include <memory>
#include <print>

using namespace Qt::StringLiterals;

namespace {

// Without --play (test hooks): --overlay multiplayer opens the main menu's multiplayer page; with
// --join it joins from there, so a refusal is shown like after a click. Runs the menu steps of the
// script ("@<ms> open:<objectName>", "@<ms> hover:<x>,<y>").
void openMenuScreen(const QQuickWindow &window, QcGameController &game, const QcAppOptions &options)
{
    if (options.play) {
        return;
    }
    game.startup()->menuShown();
    if (!options.overlays.contains(u"multiplayer"_s)) {
        return;
    }
    // The menu opens its own page.
    QObject *menu = window.findChild<QObject *>(u"mainMenu"_s);
    if (!menu) {
        qCWarning(lcStartup, "--overlay multiplayer: no main menu");
        return;
    }

    QMetaObject::invokeMethod(menu, "openMultiplayer", Q_ARG(QString, options.join.value_or(QString())), Q_ARG(QString, options.playerName),
                              Q_ARG(QString, options.password));
}

// First start of a release bundle: its example mods go, switched off, into the player's mods
// directory.
void installBundledMods()
{
    const QString bundled = QcBundledMods::directory(QCoreApplication::applicationDirPath());
    if (const QcResult<QStringList> installed = QcBundledMods::installDisabled(bundled, QcModLoader::defaultDirectory()); !installed) {
        qCWarning(lcStartup).noquote() << u"Cannot install the bundled mods:"_s << installed.error();
    } else if (!installed->isEmpty()) {
        qCInfo(lcStartup).noquote() << u"Installed bundled mods (switched off) in %1: %2"_s.arg(QcModLoader::defaultDirectory(), installed->join(u", "_s));
    }
}

void setUpMods(QcGameController &game, const QcAppOptions &options)
{
    if (!options.noMods && options.modsDirectory.isEmpty()) {
        installBundledMods();
    }
    game.library()->setModsDirectory(options.noMods                  ? QString()
                                     : options.modsDirectory.isEmpty() ? QcModLoader::defaultDirectory()
                                                                       : options.modsDirectory);
}

// Test hooks for reproducible scenes: view direction, camera, pose, spawn column, view distance.
void applySceneOptions(QcGameController &game, const QcAppOptions &options)
{
    if (options.look) {
        const QStringList parts = options.look->split(u',');
        if (parts.size() == 2) {
            game.startup()->setInitialLook(parts[0].toFloat(), parts[1].toFloat());
        }
    }
    if (options.camera) {
        if (const std::optional<QcCameraMode> mode = QcViewCamera::fromName(*options.camera)) {
            game.settings()->setCameraModeValue(*mode);
        } else {
            qCWarning(lcStartup).noquote() << u"Ignoring --camera %1: expected first, behind, front or side"_s.arg(*options.camera);
        }
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
        game.settings()->debug()->setLevel(static_cast<int>(*level));
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
void startNetworkRun(QcGameController &game, const QcAppOptions &options)
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
void startScriptedRun(QcGameController &game, const QcAppOptions &options, QcScreenshotHook *screenshot)
{
    // Screenshot runs (tests, verification) stay silent unless QTCRAFT_AUDIO=1 asks for sound;
    // QTCRAFT_AUDIO=0 or --no-audio silence any run.
    const QString audioSetting = qEnvironmentVariable("QTCRAFT_AUDIO");
    const bool audio = !options.noAudio && audioSetting != u"0"_s && (screenshot == nullptr || audioSetting == u"1"_s);
    game.startAudio(audio);
    // Screenshot runs take no input and a benchmark walks by itself, so a pad lying on the desk
    // cannot steer it; QTCRAFT_GAMEPAD=0 keeps the pads out of any run.
    const bool benchmark = qEnvironmentVariableIsSet("QTCRAFT_BENCHMARK");
    game.startGamepads(screenshot == nullptr && !benchmark && qEnvironmentVariable("QTCRAFT_GAMEPAD") != u"0"_s);
    // "@<ms> pad:<step>" lines play a virtual pad instead (decision E-583).
    if (QcScriptedPad::wanted(options.overlays)) {
        auto *pad = new QcScriptedPad(*game.gamepadUi(), &game);
        game.useScriptedGamepad(pad->source());
        game.startup()->setScriptedPad(pad);
    }
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
        game.startup()->setMenuScreenshotHook(screenshot);
    } else if (options.join) {
        game.startup()->setScreenshotHook(screenshot);
        startNetworkRun(game, options);
    } else {
        // The main menu: the screenshot waits for its steps ("@<ms> open:", "pad:").
        game.startup()->setMenuScreenshotHook(screenshot);
        if (options.handbook || options.overlays.contains(u"handbook"_s)) {
            game.handbook()->open(options.handbook.value_or(QString()));
        }
    }
}

} // namespace

void QcAppStartup::loadSettings(QcGameController &game)
{
    if (const QString packs = qEnvironmentVariable("QTCRAFT_TEXTURE_PACKS"); !packs.isEmpty()) {
        game.settings()->texturePacks()->overrideSelection(packs == u"none"_s ? QStringList() : packs.split(u',', Qt::SkipEmptyParts));
    }
    game.settings()->loadSettings();
}

void QcAppStartup::startGame(QcGameController &game, const QcAppOptions &options, QQuickWindow &window, QcOffscreenRender *offscreenRender)
{
    game.library()->refreshWorlds();
    setUpMods(game, options);
    applySceneOptions(game, options);

    QcScreenshotHook *screenshot = QcScreenshotHook::createFromEnvironment(offscreenRender ? offscreenRender->window() : &window, qApp);
    if (screenshot && offscreenRender) {
        screenshot->setGrabber(QcScreenshotHook::grabberFor(window, offscreenRender));
    }
    game.photo()->setGrabber(QcScreenshotHook::grabberFor(window, offscreenRender));
    startScriptedRun(game, options, screenshot);
    openMenuScreen(offscreenRender ? *offscreenRender->window() : window, game, options);
}
