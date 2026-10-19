#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"

#include <QList>
#include <QString>

#include <optional>

// Where texture packs are found (decision E-297): built into the game (`:/texturepacks/<folder>`),
// in the player's data folder (`<data>/texturepacks/<folder>`) and inside installed mods
// (`<mods>/<mod>/texturepacks/<folder>`, decision E-302). Listing reads only the manifests.
namespace QcTexturePackLibrary {

inline constexpr auto builtInDirectory = ":/texturepacks";
inline constexpr int maxPacks = 256;

struct Root
{
    QString path;
    QcTexturePack::Origin origin = QcTexturePack::Origin::User;
    // Put before the folder name to form the id ("builtin:", "user:", "mod:<mod>/").
    QString idPrefix;
};

struct Entry
{
    QString id;
    QcTexturePack::Origin origin = QcTexturePack::Origin::User;
    QString directory;
    // Unset when the manifest is missing or invalid; `error` says why.
    std::optional<QcTexturePackManifest> manifest;
    std::optional<QcTexturePackProblem> error;
};

// `<data>/texturepacks`.
QString userDirectory();
// The built-in folder, the user folder and the texture pack folders of the mods in `modsDirectory`
// ("" for none).
QList<Root> roots(const QString &modsDirectory);
// Every pack folder below the roots, in the order of the roots and then by folder name.
QList<Entry> scan(const QList<Root> &roots);

// A stack as loaded, and the ids that could not be loaded.
struct LoadedStack
{
    QcTexturePackStack stack;
    QStringList missing;
};

// The packs with the given ids in that order (the first has the highest priority), loaded from the
// scanned entries; ids without an entry or whose manifest is invalid are left out and listed in
// `missing`.
[[nodiscard]] LoadedStack loadStack(const QStringList &ids, const QList<Entry> &entries);

// The active ids after switching `id` on (it goes on top, with the highest priority) or off.
QStringList withActive(QStringList active, const QString &id, bool on);
// The active ids after moving `id` by `steps` places (negative: towards the highest priority).
QStringList withMoved(QStringList active, const QString &id, int steps);

} // namespace QcTexturePackLibrary
