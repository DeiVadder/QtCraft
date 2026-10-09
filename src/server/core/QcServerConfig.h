#pragma once

#include <QString>

#include <array>
#include <optional>

struct QcServerConfig
{
    QString worldName = QStringLiteral("World");
    qint64 seed = 0;
    // Radius in chunks streamed around each player.
    int viewDistance = 8;
    // Upper bound of chunks sent per client and tick, keeps ticks short while a client catches up.
    int chunksPerTick = 32;
    // Test hook: spawn above this block column (x, z) instead of the generator's spawn point.
    std::optional<std::array<int, 2>> spawnColumn;
    // Directory of the saved world; empty runs without persistence (tests, throw-away worlds).
    QString worldDirectory;
    int autosaveSeconds = 60;
    // Commands that bypass the rules (/give). On for single player; a dedicated server would
    // restrict them to operators.
    bool allowCheats = true;
    // Game rule for new and unsaved worlds; saved worlds keep the value in world.json.
    bool keepInventory = true;
    // Neighbour updates of commands (/fill) queued at most; further ones are dropped with a warning.
    qsizetype maxCommandBlockUpdates = qsizetype(1024) * 1024;
    // Pending updates written to blockupdates.qce at most (those of player actions first).
    qsizetype maxSavedBlockUpdates = qsizetype(4) * 1024 * 1024;
};
