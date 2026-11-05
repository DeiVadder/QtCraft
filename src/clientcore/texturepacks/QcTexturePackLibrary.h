// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"
#include "clientcore/texturepacks/QcTexturePackLoader.h"

#include <QList>
#include <QString>

#include <optional>

/*! \brief Where texture packs are found (decision E-297): built into the game
    (\c {:/texturepacks/<folder>}), in the player's data folder (\c {<data>/texturepacks/<folder>})
    and inside installed mods (\c {<mods>/<mod>/texturepacks/<folder>}, decision E-302).

    Listing reads only the manifests. */
namespace QcTexturePackLibrary {

/*! Resource folder of the built-in packs. */
inline constexpr auto builtInDirectory = ":/texturepacks";
/*! Most pack folders scan() lists; further folders are skipped. */
inline constexpr int maxPacks = 256;

/*! \brief A folder whose subfolders are texture packs. */
struct Root
{
    /*! Path of the folder. */
    QString path;
    /*! Origin of the packs found in it. */
    QcTexturePack::Origin origin = QcTexturePack::Origin::User;
    /*! Put before the folder name to form the id ("builtin:", "user:", "mod:<mod>/"). */
    QString idPrefix;
};

/*! \brief A pack folder found by scan(). */
struct Entry
{
    /*! Id of the pack (Root::idPrefix plus the folder name). */
    QString id;
    /*! Origin of the pack. */
    QcTexturePack::Origin origin = QcTexturePack::Origin::User;
    /*! Path of the pack folder. */
    QString directory;
    /*! Unset when the manifest is missing or invalid; \c {error} says why. */
    std::optional<QcTexturePackManifest> manifest;
    /*! Why the manifest is missing or invalid; unset when it was read. */
    std::optional<QcTexturePackProblem> error;
};

/*! Returns the user's pack folder, \c {<data>/texturepacks}. */
QString userDirectory();
/*! Returns the built-in folder, the user folder and the texture pack folders of the mods in
    \a modsDirectory ("" for none). */
QList<Root> roots(const QString &modsDirectory);
/*! Returns every pack folder below \a roots, in the order of the roots and then by folder name. */
QList<Entry> scan(const QList<Root> &roots);

/*! \brief A stack as loaded, and the ids that could not be loaded. */
struct LoadedStack
{
    /*! The loaded packs, the highest priority first. */
    QcTexturePackStack stack;
    /*! Ids that could not be loaded. */
    QStringList missing;
};

/*! Returns the packs with the given \a ids in that order (the first has the highest priority),
    loaded from the scanned \a entries; ids without an entry or whose manifest is invalid are left
    out and listed in \c {missing}; \a limits for every pack (with or without material maps). */
[[nodiscard]] LoadedStack loadStack(const QStringList &ids, const QList<Entry> &entries, const QcTexturePackLoader::Limits &limits = {});

/*! Returns the \a active ids after switching \a id on (\a on; it goes on top, with the highest
    priority) or off. */
QStringList withActive(QStringList active, const QString &id, bool on);
/*! Returns the \a active ids after moving \a id by \a steps places (negative: towards the highest
    priority). */
QStringList withMoved(QStringList active, const QString &id, int steps);

} // namespace QcTexturePackLibrary
