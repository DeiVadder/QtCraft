// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcGameMode.h"
#include "common/protocol/QcLoginMessages.h"
#include "common/protocol/QcLoginSettings.h"
#include "common/world/QcGeneratorVersion.h"
#include "server/core/QcServerLimits.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>

struct QcServerConfig
{
    QString worldName = QStringLiteral("World");
    qint64 seed = 0;
    // Generator type of the world (QcWorldGenerators); a saved world must have been made with it.
    QString generator = QStringLiteral("terrain");
    QString generatorOptions;
    // The world generation options of the world (decision E-917, QcWorldInfo::worldOptions); a saved
    // world must have been made with them. The generator and the server already run on content with
    // them set (QcWorldOptions::apply).
    QJsonObject worldOptions;
    // The version of the world generator (decision E-922, QcWorldInfo::generatorVersion); a saved
    // world must have been made with it, a new one keeps it.
    int generatorVersion = QcGeneratorVersion::current;
    // Radius in chunks streamed around each player; network clients may ask for less, never more.
    int viewDistance = 8;
    // Upper bound of chunks sent per client and tick, keeps ticks short while a client catches up.
    int chunksPerTick = 32;
    // Test hook: spawn above this block column (x, z) instead of the generator's spawn point.
    std::optional<std::array<int, 2>> spawnColumn;
    // Directory of the saved world; empty runs without persistence (tests, throw-away worlds).
    QString worldDirectory;
    int autosaveSeconds = 60;
    // The server stops ticking after no one was connected for this many seconds of the players'
    // time, saving first, and resumes when someone connects (decision E-877); 0 never pauses. A
    // dedicated server takes 60 from server.json; tests and worlds hosted in the game keep 0.
    int pauseWhenEmptySeconds = 0;
    // Test hook: no chunk leaves the world once loaded; for tests that build away from their player
    // or look at the whole world without one.
    bool keepChunksLoaded = false;
    // Test hook: only chunks whose blocks differ from the generated ones stay (the world before stage
    // 31.3, for comparison runs against its dumps).
    bool keepChangedChunks = false;
    // Test hook: chunks are read or generated at once when asked for, not in the background, so a
    // run does not depend on how fast the threads are (comparison runs).
    bool loadChunksAtOnce = false;
    // Operators besides the world's list (access.json) and the local player (decision E-244).
    QStringList operators;
    // Where /backup puts copies; empty: "backups" next to the folder of worlds.
    QString backupDirectory;
    // Game rule for new and unsaved worlds; saved worlds keep the value in world.json.
    bool keepInventory = true;
    // Game mode of players who join for the first time (decision E-282): unset, the world's default
    // (world.json) counts; set (server.json "defaultGameMode", an unsaved world) it wins, and a new
    // world is created with it.
    std::optional<QcGameMode> defaultGameMode;
    // Budgets of the simulation (decision E-349); `limitsFromServer` is set when server.json gave
    // them, then the world's own limits (world.json) are ignored.
    QcServerLimits limits;
    bool limitsFromServer = false;
    // The dedicated server's settings file (server.json): /limits writes changed limits into it
    // (decision E-848); empty elsewhere.
    QString settingsFile;
    // The admin channel shows operators the whole log of the process (a dedicated server); off, only
    // the server's categories, since a world hosted by the game shares its log with the host's game
    // (decision E-848).
    bool fullAdminLog = false;

    // Login of remote clients (decision E-252). Shown in the login challenge.
    QString serverName = QStringLiteral("QtCraft server");
    QString motd;
    // Empty: no password.
    QString password;
    // Players on the server at most, the local player included.
    int maxPlayers = 8;
    // A client that has not finished the login after this long is disconnected.
    int loginTimeoutMs = 10'000;
    // The content packs a client must have (QcContentFingerprint, with hashes); empty: id and
    // version of the packs of the loaded content.
    QList<QcPackFingerprint> contentPacks;

    void apply(const QcLoginSettings &login)
    {
        serverName = login.serverName;
        motd = login.motd;
        password = login.password;
        maxPlayers = login.maxPlayers;
        contentPacks = login.contentPacks;
    }
};
