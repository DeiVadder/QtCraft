#pragma once

#include "common/core/QcResult.h"
#include "common/diagnostics/QcDiagnosticsOptions.h"
#include "server/core/QcServerConfig.h"
#include "server/network/QcServerSettings.h"
#include "worlds/QcModLoader.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QJsonObject>
#include <QStringList>

#include <optional>

// The command line of qtcraft-headless and the steps every mode shares: the world options and
// loading the content.
namespace QcHeadlessOptions {

// The option texts below are string literals of this namespace only.
using namespace Qt::StringLiterals;

inline constexpr int defaultTimeLimitSeconds = 120;

// The command line options of qtcraft-headless.
struct Options
{
    QCommandLineOption seed{u"seed"_s, u"World seed."_s, u"number"_s, u"0"_s};
    QCommandLineOption world{u"world"_s, u"World name."_s, u"name"_s, u"Headless"_s};
    QCommandLineOption view{u"view-distance"_s, u"Chunk radius streamed to the client."_s, u"chunks"_s, u"4"_s};
    QCommandLineOption script{u"script"_s, u"Run JSON-lines commands from a file, then exit."_s, u"file"_s};
    QCommandLineOption time{u"time-limit"_s, u"Exit with failure after this many seconds."_s, u"seconds"_s,
                            QString::number(defaultTimeLimitSeconds)};
    QCommandLineOption map{u"map"_s, u"Write a top-down terrain map PNG and exit."_s, u"file"_s};
    QCommandLineOption mapRadius{u"map-radius"_s, u"Map radius in blocks."_s, u"blocks"_s, u"1024"_s};
    QCommandLineOption mapCentre{u"map-centre"_s, u"Centre of --map, --biome-map and --slice-map (the cut runs along x at this z)."_s,
                                 u"x,z"_s, u"0,0"_s};
    QCommandLineOption biomeMap{u"biome-map"_s, u"Write a top-down map with one colour per biome and exit."_s, u"file"_s};
    QCommandLineOption spawn{u"spawn"_s, u"Spawn above the block column 'x,z' (test hook)."_s, u"x,z"_s};
    QCommandLineOption slice{u"slice-map"_s,
                             u"Write a vertical cut through the world along x at z = 0 (--map-radius blocks to each side) and exit."_s,
                             u"file"_s};
    QCommandLineOption stats{u"world-stats"_s, u"Print numbers about the generated world (ores, surface, biomes, slopes) and exit."_s};
    QCommandLineOption statsRadius{u"stats-radius"_s, u"Chunk radius generated for --world-stats."_s, u"chunks"_s, u"16"_s};
    QCommandLineOption data{u"data-dir"_s, u"Data directory: worlds in <dir>/worlds, mods in <dir>/mods."_s, u"dir"_s};
    QCommandLineOption mods{u"mods-dir"_s, u"Directory with mods (default: the game's mods directory)."_s, u"dir"_s};
    QCommandLineOption noMods{u"no-mods"_s, u"Load only the base game."_s};
    QCommandLineOption sounds{u"export-sounds"_s,
                              u"Write every sound and a music piece (seeded by --seed) as WAV files into a directory and exit."_s,
                              u"dir"_s};
    QCommandLineOption handbook{u"export-handbook"_s,
                                u"Write the handbook as Markdown files into a directory and exit (base game; with --mods-dir also those mods)."_s,
                                u"dir"_s};
    QCommandLineOption language{u"language"_s, u"Language of --export-handbook, e.g. 'de' (default: en)."_s, u"locale"_s, u"en"_s};
    QCommandLineOption handbookWeb{u"export-handbook-web"_s,
                                   u"Write the data of the web handbook (every language, icons, colours) into a directory and exit (base game; with --mods-dir also those mods)."_s,
                                   u"dir"_s};
    QCommandLineOption generator{u"generator"_s, u"World type of the world started at once: terrain, flat, void or showcase."_s, u"type"_s,
                                 u"terrain"_s};
    QCommandLineOption generatorOptions{u"generator-options"_s,
                                        u"Options of --generator: the layers of a flat world ('bedrock,3*dirt,grass_block') or the platform of a void world ('platform=glass,radius=4,y=40')."_s,
                                        u"options"_s};
    QCommandLineOption worldOptions{u"world-options"_s,
                                    u"World generation options of a terrain world it starts or creates (also with --server, and for --map, "
                                    u"--biome-map, --slice-map and --world-stats): 'preset=more_villages,village_spacing=256,caves=off'."_s,
                                    u"options"_s};
    QCommandLineOption gameMode{u"game-mode"_s,
                                u"Game mode of players who join for the first time: survival, creative, adventure or spectator (with --server it overrides "
                                u"server.json and is the default of a world it creates)."_s,
                                u"mode"_s};
    QCommandLineOption tour{u"showcase-tour"_s,
                            u"Print the camera stops of the showcase tour ('name x z yaw pitch' per line) and exit."_s};
    QCommandLineOption statsLog{u"stats"_s,
                                u"Log the debug data as a JSON line 'STATS {...}' every this many milliseconds (also QTCRAFT_STATS)."_s,
                                u"ms"_s};
    QCommandLineOption console{u"console"_s,
                               u"Run a server console on standard input instead of the control interface (no bot joins; "
                               u"the time limit applies only when given)."_s};
    QCommandLineOption noEofStop{u"no-eof-stop"_s,
                                 u"With --console: the end of standard input does not stop the server (a service without input); "
                                 u"it runs until the time limit or until the process is ended."_s};
    QCommandLineOption server{u"server"_s,
                              u"Run a dedicated server: no bot, network players join over TCP, the settings come from server.json "
                              u"in the data directory (or --server-config) and the options below; SIGTERM and Ctrl+C save and stop it."_s};
    QCommandLineOption serverConfig{u"server-config"_s, u"Settings file of --server (default: <data>/server.json, made when missing)."_s,
                                    u"file"_s};
    QCommandLineOption port{u"port"_s, u"TCP port of --server (0: any free port)."_s, u"port"_s};
    QCommandLineOption bind{u"bind"_s, u"Address --server listens on (default: every interface, IPv4 and IPv6)."_s, u"address"_s};
    QCommandLineOption maxPlayers{u"max-players"_s, u"Players --server lets in at most."_s, u"count"_s};
    QCommandLineOption password{u"password"_s,
                                u"Password of --server or of the server --join logs in to (also QTCRAFT_SERVER_PASSWORD; empty: none). "
                                u"Visible to other users of this computer in the process list; prefer the variable or server.json."_s,
                                u"password"_s};
    QCommandLineOption motd{u"motd"_s, u"Message of the day of --server."_s, u"text"_s};
    QCommandLineOption join{u"join"_s,
                            u"Join the server at 'host', 'host:port' or '[v6]:port' over TCP instead of starting a world: the "
                            u"control-interface bot (standard input or --script) plays there. Exit code 3 when the join fails, 4 when "
                            u"the server ends the connection."_s,
                            u"address"_s};
    QCommandLineOption playerName{u"player-name"_s, u"Name of the bot that joins with --join."_s, u"name"_s, u"Bot"_s};
    QCommandLineOption icon{u"export-icon"_s,
                            u"Write the application icon (base game textures) as the PNG images of a macOS .iconset directory, or as a "
                            u"Windows icon when the path ends in .ico, and exit."_s,
                            u"path"_s};

    QCommandLineOption exportTextures{u"export-textures"_s,
                                      u"Write the content's textures as an image texture pack (texturepack.json, textures/<namespace>/<name>.png, "
                                      u"preview.png) into a directory and exit."_s,
                                      u"dir"_s};
    QCommandLineOption textureResolution{u"texture-resolution"_s, u"Resolution of --export-textures (16 to 512)."_s, u"pixels"_s, u"32"_s};
    QCommandLineOption texturePacks{u"texture-packs"_s,
                                    u"Texture packs drawn over the content for --export-textures, highest priority first "
                                    u"(e.g. builtin:qtcraft_hd_32)."_s,
                                    u"ids"_s};
    QCommandLineOption textureKeys{u"texture-keys"_s, u"Only these textures for --export-textures (comma-separated names)."_s, u"names"_s};
    QCommandLineOption textureName{u"texture-pack-name"_s, u"Name in the manifest written by --export-textures."_s, u"name"_s};
    // --log-file, --no-log-file, --log-rules, --health-log, --full-dump (decision E-834).
    QcDiagnosticsOptions diagnostics;
    QCommandLineOption writeDiagnostics{u"write-diagnostics"_s,
                                        u"Write a diagnostics bundle (logs, crash reports, server.json and settings without passwords, mods and "
                                        u"worlds) of the data directory into <data>/diagnostics and exit; a running --server with --console "
                                        u"does the same on the line 'diagnostics'."_s};

    QCommandLineOption atlasStats{u"atlas-stats"_s,
                                  u"Build the texture atlas without packs and with every built-in texture pack, print size, memory and "
                                  u"build time, and exit."_s};

    // In the order of the help text.
    void addTo(QCommandLineParser &parser) const
    {
        parser.addOptions({sounds, icon});
        parser.addOptions({exportTextures, textureResolution, texturePacks, textureKeys, textureName, atlasStats});
        parser.addOptions({handbook, language, handbookWeb});
        parser.addOptions({stats, statsRadius, biomeMap, slice, spawn, tour});
        parser.addOptions({seed, world, generator, generatorOptions, worldOptions, gameMode, view, script, console, noEofStop, time, map, mapRadius, mapCentre, data, mods,
                           noMods, statsLog});
        parser.addOptions({server, serverConfig, port, bind, maxPlayers, password, motd});
        parser.addOptions({join, playerName});
        parser.addOptions(diagnostics.all());
        parser.addOption(writeDiagnostics);
    }
};

void printError(const QString &message);
void printLines(const QStringList &lines);

// What a local or joining run starts with: the server's configuration and the time limit in seconds.
struct RunConfig
{
    QcServerConfig server;
    int timeLimit = 0;
};

// Seed, world name, view distance, spawn column, game mode and the time limit; nullopt on a bad value.
[[nodiscard]] std::optional<RunConfig> readConfig(const QCommandLineParser &parser, const Options &options);
// The world generation options of --world-options for a world of `type` (decision E-917): every
// option, the named ones as given; without the option none (the content's values).
[[nodiscard]] QcResult<QJsonObject> worldOptionsOf(const QString &text, const QString &type, const QcContentRegistry &content);
// The data directory: --data-dir, else QcDataRoot.
QString dataDirectory(const QCommandLineParser &parser, const Options &options);
// The base game with the mods of --mods-dir, else of the data directory's mods folder (none with
// --no-mods); std::nullopt (after printing the reason) when it cannot be loaded.
[[nodiscard]] std::optional<QcModLoader::Result> loadContent(const QCommandLineParser &parser, const Options &options);

// The settings of --server: the file (made with the defaults when the default file is missing),
// then QTCRAFT_SERVER_PASSWORD, then the options; std::nullopt (after printing why) when invalid.
[[nodiscard]] std::optional<QcServerSettings> serverSettings(const QCommandLineParser &parser, const Options &options);

} // namespace QcHeadlessOptions
