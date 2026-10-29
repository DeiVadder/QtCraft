// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcWorldInfo.h"

#include <QString>

#include <array>
#include <optional>

// What a program asks of the local server when it opens a world (decision E-705).
// QcLocalServerHost turns it into the server's configuration and adds the server's rules for it.
struct QcWorldStart
{
    // The world as listed or chosen: name, seed, generator with its options, and the default game
    // mode, which counts only for an unsaved world (a saved one keeps its own in world.json).
    QcWorldInfo world;
    // Directory of the saved world; empty runs the world unsaved (test hooks).
    QString directory;
    // The player's view distance in chunks (QcGameSettings). No default: every caller passes the
    // player's setting, so a start cannot silently use a different distance.
    int viewDistance;
    // Test hook: the player spawns above this block column (x, z) instead of the generator's spawn point.
    std::optional<std::array<int, 2>> spawnColumn;
};
