#include "clientcore/texturepacks/QcTextureStackBuild.h"

#include "clientcore/texturepacks/QcTexturePackLoader.h"

#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>

namespace QcTextureStackBuild {

namespace {

// The active packs in priority order: reused ones from the request, the others loaded now.
QcTexturePackStack loadPacks(const Request &request, const QList<QcTexturePackLibrary::Entry> &entries, QStringList &missing)
{
    QStringList toLoad;
    for (const QString &id : request.ids) {
        if (!request.loaded.contains(id)) {
            toLoad.append(id);
        }
    }
    const QcTexturePackStack fresh = QcTexturePackLibrary::loadStack(toLoad, entries, missing);

    QcTexturePackStack stack;
    for (const QString &id : request.ids) {
        if (const QcTexturePackRef known = request.loaded.value(id)) {
            stack.append(known);
            continue;
        }
        const auto loaded = std::ranges::find(fresh, id, &QcTexturePack::id);
        if (loaded != fresh.end()) {
            stack.append(*loaded);
        }
    }
    return stack;
}

// Keeps the packs whose images fit the budget, the highest priority first.
QcTexturePackStack withinBudget(const QcTexturePackStack &stack, qint64 budget, QStringList &overBudget)
{
    QcTexturePackStack kept;
    qint64 used = 0;
    for (const QcTexturePackRef &pack : stack) {
        if (used + pack->decodedBytes > budget) {
            overBudget.append(pack->id);
            continue;
        }
        used += pack->decodedBytes;
        kept.append(pack);
    }
    return kept;
}

// Previews of all listed packs; a file whose modification time did not change is not read again.
QHash<QString, Preview> previewsOf(const QList<QcTexturePackLibrary::Entry> &entries, const QHash<QString, Preview> &known)
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
    result.stack = withinBudget(loadPacks(request, result.entries, result.missing), request.memoryBudget, result.overBudget);
    result.previews = previewsOf(result.entries, request.previews);
    result.loadMs = timer.restart();

    if (request.content) {
        result.atlas = std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(*request.content, result.stack, request.options));
        result.buildMs = timer.elapsed();
    }
    return result;
}

} // namespace QcTextureStackBuild
