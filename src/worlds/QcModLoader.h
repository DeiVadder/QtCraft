// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "worlds/QcModStatus.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

/*! \brief Finds data-only mods (content packs) in a directory, orders them by their dependencies
    and loads them after the base game into one registry.

    Each mod is a sub-directory with a pack.json manifest; a mod that cannot be loaded is skipped
    together with every mod that depends on it, the rest of the game still starts. */
class QcModLoader
{
public:
    /*! Upper bound for the number of mod directories looked at. */
    static constexpr int maxMods = 256;
    /*! Upper bound for the number of mods loaded (each costs a full check, whether accepted or
        not). */
    static constexpr int maxLoadedMods = 64;
    /*! Optional file in the mods directory: {"disabled": ["mod_id", ...]}. */
    static constexpr auto settingsFile = "mods.json";

    /*! \brief A load that succeeded: the content, the state of every mod and the warnings. */
    struct Result
    {
        /*! The base game and every loaded mod in one registry. */
        std::shared_ptr<QcContentRegistry> content;
        /*! Every mod directory found, in load order for loaded mods, then the others by
            directory. */
        QList<QcModStatus> mods;
        /*! Warnings of the base game, of the mods (also failed ones) and of references between
            packs. */
        QStringList warnings;
    };

    /*! \brief A load that failed: why, and the warnings collected until then. */
    struct Failure
    {
        /*! Why the base game could not be loaded. */
        QString error;
        /*! Warnings collected before the failure. */
        QStringList warnings;
    };

    /*! The outcome of load(): the result, or the failure. */
    using Loaded = std::expected<Result, Failure>;

    /*! Returns the warnings of \a loaded, whether it succeeded or not (callers report them before
        the outcome). */
    static const QStringList &warningsOf(const Loaded &loaded) { return loaded ? loaded->warnings : loaded.error().warnings; }

    /*! Returns \c {<data root>/mods} (see QcWorldLibrary::dataRoot()). */
    static QString defaultDirectory();

    /*! Reads all manifests below \a modsDirectory and decides the load order without loading
        content. An empty or missing directory gives an empty list. */
    static QList<QcModStatus> discover(const QString &modsDirectory);

    /*! Orders \a candidates so that every mod comes after its dependencies (ties by id, so the
        order is stable). Mods with unknown, failed, disabled or too old dependencies, duplicate
        ids, cycles or the base game's id fail; mods whose id is in \a disabled are disabled.
        \a available holds the packs loaded before (base). Returns the state of every candidate. */
    static QList<QcModStatus> resolve(const QList<QcContentPackInfo> &candidates, const QStringList &disabled,
                                      const QList<QcPackReference> &available);

    /*! Base game plus every loadable mod. Fails only when the base game cannot be loaded; an
        empty \a modsDirectory loads no mods. */
    [[nodiscard]] static Loaded load(const QString &modsDirectory);

    /*! Returns the ids on the disabled list in \c {mods.json} of \a modsDirectory; empty when the
        file is missing, unreadable or too large. */
    static QStringList disabledMods(const QString &modsDirectory);
    /*! Takes the mod \a id off the disabled list in \a modsDirectory when \a enabled is \c true,
        else puts it on, and writes the list (creating the directory); takes effect the next time
        content is loaded. Returns an error when the directory or file cannot be written. */
    [[nodiscard]] static QcStatus setModEnabled(const QString &modsDirectory, const QString &id, bool enabled);
};
