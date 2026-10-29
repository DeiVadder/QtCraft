// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/loader/QcContentPackLoader.h"

#include <QString>
#include <QStringList>

// Outcome of one mod directory after discovery and loading.
struct QcModStatus
{
    enum class State { Loaded, Disabled, Failed };

    // `info.id` may be empty when the manifest could not be read; `info.directory` is always set.
    QcContentPackInfo info;
    State state = State::Failed;
    // Why the mod was not loaded (Failed only).
    QString reason;
    // Entries of a loaded mod that were skipped.
    QStringList warnings;
};
