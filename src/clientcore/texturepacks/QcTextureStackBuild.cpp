// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/texturepacks/QcTextureStackBuild.h"

#include "clientcore/texturepacks/QcTexturePackLoader.h"

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>

namespace QcTextureStackBuild {

namespace {

// The active packs in priority order: reused ones from the request, the others loaded now; the ids
// that could not be loaded.
[[nodiscard]] QcTexturePackLibrary::LoadedStack loadPacks(const Request &request, const QList<QcTexturePackLibrary::Entry> &entries)
{
    QStringList toLoad;
    for (const QString &id : request.ids) {
        if (!request.loaded.contains(id)) {
            toLoad.append(id);
        }
    }
    // The material maps only while the atlas carries them (decision E-908).
    QcTexturePackLoader::Limits limits;
    limits.materialMaps = request.options.materials;
    QcTexturePackLibrary::LoadedStack fresh = QcTexturePackLibrary::loadStack(toLoad, entries, limits);

    QcTexturePackStack stack;
    for (const QString &id : request.ids) {
        if (const QcTexturePackRef known = request.loaded.value(id)) {
            stack.append(known);
            continue;
        }
        const auto loaded = std::ranges::find(fresh.stack, id, &QcTexturePack::id);
        if (loaded != fresh.stack.end()) {
            stack.append(*loaded);
        }
    }
    return {std::move(stack), std::move(fresh.missing)};
}

// The packs whose images fit the budget, the highest priority first, and the ids of the others.
struct Budgeted
{
    QcTexturePackStack kept;
    QStringList overBudget;
};

[[nodiscard]] Budgeted withinBudget(const QcTexturePackStack &stack, qint64 budget)
{
    Budgeted result;
    qint64 used = 0;
    for (const QcTexturePackRef &pack : stack) {
        if (used + pack->decodedBytes > budget) {
            result.overBudget.append(pack->id);
            continue;
        }
        used += pack->decodedBytes;
        result.kept.append(pack);
    }
    return result;
}

// Previews of all listed packs; a file whose modification time did not change is not read again.
[[nodiscard]] QHash<QString, Preview> previewsOf(const QList<QcTexturePackLibrary::Entry> &entries, const QHash<QString, Preview> &known)
{
    QHash<QString, Preview> previews;
    for (const QcTexturePackLibrary::Entry &entry : entries) {
        if (!entry.manifest || entry.manifest->preview.isEmpty()) {
            continue;
        }

        const QDateTime modified = QFileInfo(entry.directory + u'/' + entry.manifest->preview).lastModified();
        const auto cached = known.constFind(entry.directory);
        if (cached != known.constEnd() && cached->modified == modified) {
            previews.insert(entry.directory, *cached);
            continue;
        }
        previews.insert(entry.directory, {modified, QcTexturePackLoader::readPreview(entry.directory, *entry.manifest).value_or(QImage())});
    }
    return previews;
}

} // namespace

Result run(const Request &request)
{
    Result result;
    result.generation = request.generation;
    QElapsedTimer timer;
    timer.start();

    result.entries = request.rescan ? QcTexturePackLibrary::scan(*request.rescan) : request.entries;
    QcTexturePackLibrary::LoadedStack loaded = loadPacks(request, result.entries);
    result.missing = std::move(loaded.missing);
    Budgeted budgeted = withinBudget(loaded.stack, request.memoryBudget);
    result.stack = std::move(budgeted.kept);
    result.overBudget = std::move(budgeted.overBudget);
    result.previews = previewsOf(result.entries, request.previews);
    result.loadMs = timer.restart();

    if (!request.content) {
        return result;
    }
    result.atlas = request.colour ? request.colour
                                  : std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(*request.content, result.stack, request.options));
    const qint64 colourMs = timer.restart();
    if (request.options.materials && !request.colourOnly) {
        result.materials = std::make_shared<const QcMaterialAtlas>(QcMaterialAtlas::build(*result.atlas, *request.content, result.stack));
        result.materialsMs = timer.elapsed();
    }
    result.buildMs = colourMs + result.materialsMs;
    return result;
}

} // namespace QcTextureStackBuild
