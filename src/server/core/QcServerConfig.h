#pragma once

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
    // Radius in chunks streamed around each player.
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
    // Neighbour updates of commands (/fill) queued at most; further ones are dropped with a warning.
    qsizetype maxCommandBlockUpdates = qsizetype(1024) * 1024;
    // Pending updates written to blockupdates.qce at most (those of player actions first).
    qsizetype maxSavedBlockUpdates = qsizetype(4) * 1024 * 1024;
};
