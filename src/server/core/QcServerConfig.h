#pragma once

#include "QcGameMode.h"
#include "QcLoginMessages.h"
#include "QcServerLimits.h"

#include <QString>
#include <QStringList>

#include <array>
#include <optional>

// The login settings of a server open to network players (decision E-252); a single-player world
// opened to the LAN gets them when it starts listening.
struct QcLoginSettings
{
    QString serverName = QStringLiteral("QtCraft server");
    QString motd;
    // Empty: no password.
    QString password;
    // Players on the server at most, the local player included.
    int maxPlayers = 8;
    // The content packs a client must have (QcContentFingerprint, with hashes); empty: id and
    // version of the packs of the loaded content.
    QList<QcPackFingerprint> contentPacks;
};

struct QcServerConfig
{
    QString worldName = QStringLiteral("World");
    qint64 seed = 0;
    // Generator type of the world (QcWorldGenerators); a saved world must have been made with it.
    QString generator = QStringLiteral("terrain");
    QString generatorOptions;
    // Radius in chunks streamed around each player; network clients may ask for less, never more.
    int viewDistance = 8;
    // Upper bound of chunks sent per client and tick, keeps ticks short while a client catches up.
    int chunksPerTick = 32;
    // Test hook: spawn above this block column (x, z) instead of the generator's spawn point.
    std::optional<std::array<int, 2>> spawnColumn;
    // Directory of the saved world; empty runs without persistence (tests, throw-away worlds).
    QString worldDirectory;
    int autosaveSeconds = 60;
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
