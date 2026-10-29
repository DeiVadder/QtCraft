// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

class QcChunkStreaming;
class QcPlayerHealth;
class QcServerGameModes;
class QcServerInventory;
class QcServerPersistence;
struct QcServerClient;
class QcServerState;

// A client the login admitted becomes a player (decision E-367): restored from his save (QcPlayerRecords)
// or spawned at the spawn point in the world's default game mode, told everything a client needs to
// play (Welcome, content, game mode, inventory, health, food, time, warnings about the world) and
// given the chunks around him. A save that cannot be read ends the connection with the reason.
class QcPlayerJoin
{
public:
    struct Parts
    {
        QcServerPersistence &persistence;
        QcServerInventory &inventory;
        QcPlayerHealth &health;
        QcServerGameModes &gameModes;
        QcChunkStreaming &streaming;
        // Sends the time of day (QcServer knows the tick rate).
        std::function<void(QcServerClient &client)> sendTime;
    };

    QcPlayerJoin(QcServerState &state, Parts parts);

    void join(QcServerClient &client);

private:
    // Restores a saved player; false when there is none (or no storage). Closes the connection
    // when the saved data cannot be read.
    [[nodiscard]] bool restore(QcServerClient &client);
    void sendWelcome(QcServerClient &client, bool rejoined);
    // The first problems found while opening the world (missing mods, unknown blocks).
    void sendOpenWarnings(QcServerClient &client) const;
    // Tells an operator who joins that access.json was damaged when the world opened (E-244).
    void sendAccessWarning(QcServerClient &client);

    QcServerState &m_state;
    Parts m_parts;
};
