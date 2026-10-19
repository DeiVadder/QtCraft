#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "server/content/QcModStatus.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// Finds data-only mods (content packs) in a directory, orders them by their dependencies and loads
// them after the base game into one registry. Each mod is a sub-directory with a pack.json manifest;
// a mod that cannot be loaded is skipped together with every mod that depends on it, the rest of
// the game still starts.
class QcModLoader
{
public:
    // Upper bound for the number of mod directories looked at.
    static constexpr int maxMods = 256;
    // Upper bound for the number of mods loaded (each costs a full check, whether accepted or not).
    static constexpr int maxLoadedMods = 64;
    // Optional file in the mods directory: {"disabled": ["mod_id", ...]}.
    static constexpr auto settingsFile = "mods.json";

    struct Result
    {
        std::shared_ptr<QcContentRegistry> content;
        // Every mod directory found, in load order for loaded mods, then the others by directory.
        QList<QcModStatus> mods;
        // Warnings of the base game, of the mods (also failed ones) and of references between packs.
        QStringList warnings;
    };

    // A load that failed: why, and the warnings collected until then.
    struct Failure
    {
        QString error;
        QStringList warnings;
    };

    using Loaded = std::expected<Result, Failure>;

    // The warnings of a load, whether it succeeded or not (callers report them before the outcome).
    static const QStringList &warningsOf(const Loaded &loaded) { return loaded ? loaded->warnings : loaded.error().warnings; }

    // <data root>/mods (see QcWorldLibrary::dataRoot()).
    static QString defaultDirectory();

    // Reads all manifests below `modsDirectory` and decides the load order without loading content.
    // An empty or missing directory gives an empty list.
    static QList<QcModStatus> discover(const QString &modsDirectory);

    // Orders `candidates` so that every mod comes after its dependencies (ties by id, so the
    // order is stable). Mods with unknown, failed, disabled or too old dependencies, duplicate
    // ids, cycles or the base game's id fail. `available` holds the packs loaded before (base).
    static QList<QcModStatus> resolve(const QList<QcContentPackInfo> &candidates, const QStringList &disabled,
                                      const QList<QcPackReference> &available);

    // Base game plus every loadable mod. Fails only when the base game cannot be loaded; an empty
    // `modsDirectory` loads no mods.
    [[nodiscard]] static Loaded load(const QString &modsDirectory);

    static QStringList disabledMods(const QString &modsDirectory);
    // Writes the disabled list; takes effect the next time content is loaded.
    [[nodiscard]] static QcStatus setModEnabled(const QString &modsDirectory, const QString &id, bool enabled);
};
