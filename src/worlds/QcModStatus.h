// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/loader/QcContentPackLoader.h"

#include <QString>
#include <QStringList>

/*! \brief Outcome of one mod directory after discovery and loading. */
struct QcModStatus
{
    /*! \brief Whether the mod was loaded. */
    enum class State {
        Loaded,   // Its content is part of the registry.
        Disabled, // On the disabled list of the mods directory; not loaded.
        Failed    // Could not be loaded; reason says why.
    };

    /*! \c {info.id} may be empty when the manifest could not be read; \c {info.directory} is
        always set. */
    QcContentPackInfo info;
    /*! Whether the mod was loaded, switched off or failed. */
    State state = State::Failed;
    /*! Why the mod was not loaded (Failed only). */
    QString reason;
    /*! Entries of a loaded mod that were skipped. */
    QStringList warnings;
};
