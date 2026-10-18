#include "app/startup/QcAppOptions.h"

#include "clientcore/settings/QcDebugOptions.h"
#include "common/gameplay/QcGameMode.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QCommandLineParser>
#include <QGuiApplication>

using namespace Qt::StringLiterals;

namespace {

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
QString worldTypeError(const QcAppOptions &options, bool flatLayersGiven)
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
QString debugOptionsError(const QcAppOptions &options)
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

} // namespace

QcAppOptions QcAppOptions::parse(const QGuiApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"QtCraft voxel sandbox"_s);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions(commandLineOptions());
    parser.process(app);

    const auto optional = [&parser](const QString &name) { return parser.isSet(name) ? std::optional(parser.value(name)) : std::nullopt; };
    QcAppOptions options;
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

QString QcAppOptions::offscreenRunError()
{
    if (qEnvironmentVariableIntValue("QTCRAFT_BENCHMARK") > 0) {
        return u"Benchmarks measure the real window and do not run offscreen (set QTCRAFT_OFFSCREEN=0, decision E-229)"_s;
    }
    if (qEnvironmentVariableIsEmpty("QTCRAFT_SCREENSHOT")) {
        return u"An offscreen run needs QTCRAFT_SCREENSHOT: without a window nothing would end it"_s;
    }
    return {};
}
