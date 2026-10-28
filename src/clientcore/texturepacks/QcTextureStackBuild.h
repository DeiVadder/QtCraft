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

// One switch of texture packs (decision E-301): optionally lists the pack folders again, loads the
// active packs that are not loaded yet, reads the previews that changed and builds the atlas for
// the content. Only reads its request, whose parts are immutable and shared, so it runs in a worker
// thread while the game goes on with the previous atlas. The generation is handed through so the
// caller can drop results that a later change made out of date.
namespace QcTextureStackBuild {

// Decoded images of all active packs together; packs beyond it (the lowest priority first) are
// left out with a notice.
inline constexpr qint64 stackMemoryBudget = qint64{384} * 1024 * 1024;

// A preview image read before, with the modification time of its file.
struct Preview
{
    QDateTime modified;
    QImage image;
};

struct Request
{
    quint64 generation = 0;
    // Active pack ids, the highest priority first.
    QStringList ids;
    // Set: list the packs below these roots first; else use `entries`.
    std::optional<QList<QcTexturePackLibrary::Root>> rescan;
    QList<QcTexturePackLibrary::Entry> entries;
    // Packs loaded before, by id; reused instead of reading their files again.
    QHash<QString, QcTexturePackRef> loaded;
    // Previews read before, by pack folder.
    QHash<QString, Preview> previews;
    // Null: only load the packs (in the menu, before a world has content).
    std::shared_ptr<const QcContentRegistry> content;
    // With options.materials the packs are read with their material maps and the maps are built
    // (Result::materials), unless `colourOnly` (the maps follow in a run of their own, so the world
    // starts without waiting for them); with `colour` set only the maps of that atlas are built
    // (decision E-912).
    QcTextureAtlas::Options options;
    bool colourOnly = false;
    std::shared_ptr<const QcTextureAtlas> colour;
    qint64 memoryBudget = stackMemoryBudget;
};

struct Result
{
    quint64 generation = 0;
    QList<QcTexturePackLibrary::Entry> entries;
    QcTexturePackStack stack;
    // Ids without a usable pack (gone from disk, invalid manifest).
    QStringList missing;
    // Ids left out because the active packs together exceed the memory budget.
    QStringList overBudget;
    // Previews by pack folder (unchanged ones taken over from the request).
    QHash<QString, Preview> previews;
    std::shared_ptr<const QcTextureAtlas> atlas;
    // The atlas's material maps, handed on apart from it: whoever uploads them lets them go
    // (decision E-912). Null without options.materials or with colourOnly.
    std::shared_ptr<const QcMaterialAtlas> materials;
    qint64 loadMs = 0;
    // The colour atlas and the maps together; the maps alone in materialsMs.
    qint64 buildMs = 0;
    qint64 materialsMs = 0;
};

Result run(const Request &request);

} // namespace QcTextureStackBuild
