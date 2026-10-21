#pragma once

#include <QLoggingCategory>

// The logging categories of qc_server, each defined once in QcServerLogging.cpp (decision E-680).
// Users filter them by name, e.g. QT_LOGGING_RULES="qtcraft.server.debug=true".

// "qtcraft.commands": chat commands and their effects.
Q_DECLARE_LOGGING_CATEGORY(lcCommands)

// "qtcraft.content": loading content and mods, the content fingerprint.
Q_DECLARE_LOGGING_CATEGORY(lcContent)

// "qtcraft.gamemodes": game mode changes.
Q_DECLARE_LOGGING_CATEGORY(lcGameModes)

// "qtcraft.items": item entities in the world.
Q_DECLARE_LOGGING_CATEGORY(lcItems)

// "qtcraft.lag": ticks longer than 50 ms with their longest phases (QcLagReport, decision E-834).
Q_DECLARE_LOGGING_CATEGORY(lcLag)

// "qtcraft.network": the TCP server: listening, connections.
Q_DECLARE_LOGGING_CATEGORY(lcNetwork)

// "qtcraft.plants": plant growth.
Q_DECLARE_LOGGING_CATEGORY(lcPlants)

// "qtcraft.server": the server's world, tick, chunks, storage, login and blocks.
Q_DECLARE_LOGGING_CATEGORY(lcServer)

// "qtcraft.trades": villager trades.
Q_DECLARE_LOGGING_CATEGORY(lcTrades)
