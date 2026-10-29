// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QString>

// A chunk loader block of a world (decision E-377): where it stands and who placed it
// (QcChunkLoaders::Loader on the server).
struct QcPlacedChunkLoader
{
    QcBlockPos pos;
    // The player who placed it; empty for commands and structures.
    QString owner;

    bool operator==(const QcPlacedChunkLoader &) const = default;
};
