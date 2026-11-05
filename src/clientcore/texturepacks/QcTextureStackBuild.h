// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/materials/QcMaterialAtlas.h"
#include "clientcore/texturepacks/QcTexturePack.h"
#include "clientcore/texturepacks/QcTexturePackLibrary.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QDateTime>
#include <QHash>
#include <QStringList>

#include <memory>
#include <optional>

/*! \brief One switch of texture packs (decision E-301): optionally lists the pack folders again,
    loads the active packs that are not loaded yet, reads the previews that changed and builds the
    atlas for the content.

    Only reads its request, whose parts are immutable and shared, so it runs in a worker thread
    while the game goes on with the previous atlas. The generation is handed through so the
    caller can drop results that a later change made out of date. */
namespace QcTextureStackBuild {

/*! Decoded images of all active packs together; packs beyond it (the lowest priority first) are
    left out with a notice. */
inline constexpr qint64 stackMemoryBudget = qint64{384} * 1024 * 1024;

/*! \brief A preview image read before, with the modification time of its file. */
struct Preview
{
    /*! Modification time of the preview file. */
    QDateTime modified;
    /*! The decoded preview. */
    QImage image;
};

/*! \brief What one run() is asked to do. */
struct Request
{
    /*! Handed through to Result::generation. */
    quint64 generation = 0;
    /*! Active pack ids, the highest priority first. */
    QStringList ids;
    /*! Set: list the packs below these roots first; else use \c {entries}. */
    std::optional<QList<QcTexturePackLibrary::Root>> rescan;
    /*! The scanned pack folders when \c {rescan} is unset. */
    QList<QcTexturePackLibrary::Entry> entries;
    /*! Packs loaded before, by id; reused instead of reading their files again. */
    QHash<QString, QcTexturePackRef> loaded;
    /*! Previews read before, by pack folder. */
    QHash<QString, Preview> previews;
    /*! Null: only load the packs (in the menu, before a world has content). */
    std::shared_ptr<const QcContentRegistry> content;
    /*! With options.materials the packs are read with their material maps and the maps are built
        (Result::materials), unless \c {colourOnly} (the maps follow in a run of their own, so
        the world starts without waiting for them); with \c {colour} set only the maps of that
        atlas are built (decision E-912). */
    QcTextureAtlas::Options options;
    /*! Build only the colour atlas, not the maps. */
    bool colourOnly = false;
    /*! Colour atlas built before; set, run() builds only its maps. */
    std::shared_ptr<const QcTextureAtlas> colour;
    /*! Decoded bytes the active packs may take together. */
    qint64 memoryBudget = stackMemoryBudget;
};

/*! \brief What one run() produced. */
struct Result
{
    /*! The request's generation. */
    quint64 generation = 0;
    /*! The pack folders, listed again or taken over from the request. */
    QList<QcTexturePackLibrary::Entry> entries;
    /*! The loaded packs within the budget, the highest priority first. */
    QcTexturePackStack stack;
    /*! Ids without a usable pack (gone from disk, invalid manifest). */
    QStringList missing;
    /*! Ids left out because the active packs together exceed the memory budget. */
    QStringList overBudget;
    /*! Previews by pack folder (unchanged ones taken over from the request). */
    QHash<QString, Preview> previews;
    /*! The colour atlas (the request's colour when it had one); null without content. */
    std::shared_ptr<const QcTextureAtlas> atlas;
    /*! The atlas's material maps, handed on apart from it: whoever uploads them lets them go
        (decision E-912). Null without options.materials or with colourOnly. */
    std::shared_ptr<const QcMaterialAtlas> materials;
    /*! Milliseconds for listing and loading the packs and reading the previews. */
    qint64 loadMs = 0;
    /*! The colour atlas and the maps together; the maps alone in materialsMs. */
    qint64 buildMs = 0;
    /*! Milliseconds for the material maps. */
    qint64 materialsMs = 0;
};

/*! Returns the packs, previews and atlases of \a request; safe in a worker thread. */
Result run(const Request &request);

} // namespace QcTextureStackBuild
