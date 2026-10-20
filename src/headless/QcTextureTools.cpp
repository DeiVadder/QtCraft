#include "headless/QcTextureTools.h"

#include "clientcore/texturepacks/QcTexturePackLibrary.h"
#include "clientcore/texturepacks/QcTextureSources.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "headless/QcHeadlessOptions.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <algorithm>
#include <limits>
#include <optional>
#include <print>

using namespace Qt::StringLiterals;

namespace QcTextureTools {

namespace {

// The tiles of the preview image, left to right and top to bottom.
const QStringList previewKeys = {u"qtcraft:grass_side"_s, u"qtcraft:stone"_s,     u"qtcraft:oak_planks"_s, u"qtcraft:cobblestone"_s,
                                 u"qtcraft:oak_log"_s,    u"qtcraft:iron_ore"_s,  u"qtcraft:bricks"_s,     u"qtcraft:iron_pickaxe"_s};

// The stack of the packs `ids`, after printing their problems; nullopt if one is unknown or invalid.
[[nodiscard]] std::optional<QcTexturePackStack> loadPacks(const QStringList &ids, const QString &modsDirectory)
{
    const QcTexturePackLibrary::LoadedStack loaded = QcTexturePackLibrary::loadStack(ids, QcTexturePackLibrary::scan(QcTexturePackLibrary::roots(modsDirectory)));
    const QcTexturePackStack &stack = loaded.stack;
    for (const QString &id : loaded.missing) {
        QcHeadlessOptions::printError(u"Unknown or invalid texture pack: "_s + id);
    }
    for (const QcTexturePackRef &pack : stack) {
        for (const QcTexturePackProblem &problem : pack->problems) {
            QcHeadlessOptions::printError(u"%1: %2"_s.arg(pack->id, problem.englishText()));
        }
    }
    return loaded.missing.isEmpty() ? std::optional(stack) : std::nullopt;
}

QJsonObject manifestFor(const ExportRequest &request)
{
    return {
        {u"format"_s, 1},
        {u"name"_s, request.name.isEmpty() ? u"Exported textures %1x"_s.arg(request.resolution) : request.name},
        {u"description"_s, u"Textures exported by qtcraft-headless --export-textures."_s},
        {u"resolution"_s, request.resolution},
        // The game's textures are licensed like the code (decision E-303); an exported pack keeps that.
        {u"license"_s, u"GPL-3.0-or-later"_s},
        {u"credits"_s, QJsonArray{QJsonObject{{u"author"_s, u"QtCraft"_s}}}},
        {u"preview"_s, u"preview.png"_s},
    };
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).path()) && file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QImage previewOf(const QHash<QString, QImage> &tiles, int resolution)
{
    QImage preview(resolution * 4, resolution * 2, QImage::Format_RGBA8888);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    for (qsizetype i = 0; i < previewKeys.size(); ++i) {
        painter.drawImage(int(i % 4) * resolution, int(i / 4) * resolution, tiles.value(previewKeys[i]));
    }
    return preview;
}

} // namespace

int exportPack(const QcContentRegistry &content, const ExportRequest &request)
{
    const std::optional<QcTexturePackStack> loaded = loadPacks(request.packs, request.modsDirectory);
    if (!loaded || !QcTexturePackManifest::isValidResolution(request.resolution)) {
        QcHeadlessOptions::printError(u"Nothing exported (check the packs and that the resolution is a power of two from 16 to 512)"_s);
        return 1;
    }

    const QDir directory(request.directory);
    QHash<QString, QImage> written;
    for (const QcTextureDefinition &definition : content.assets().textures()) {
        if (!request.keys.isEmpty() && !request.keys.contains(definition.name)) {
            continue;
        }

        // Only names a pack can hold: one plain folder and one plain file name, never a path out of
        // the target directory (content names may contain '/' and '.').
        const QString space = definition.name.section(u':', 0, 0);
        const QString name = definition.name.section(u':', 1);
        if (!QcTexturePackManifest::isPlainFileName(space) || !QcTexturePackManifest::isPlainFileName(name)) {
            QcHeadlessOptions::printError(u"Skipped %1: a texture pack cannot hold this name"_s.arg(definition.name));
            continue;
        }

        const QcResult<QcTextureSources::Resolved> resolved = QcTextureSources::resolve(definition, *loaded, request.resolution);
        const QString path = directory.filePath(u"textures/%1/%2.png"_s.arg(space, name));
        if (!resolved || !QDir().mkpath(QFileInfo(path).path()) || !resolved->image.save(path, "PNG")) {
            QcHeadlessOptions::printError(u"Cannot export %1 %2"_s.arg(definition.name, resolved ? QString() : resolved.error()));
            return 1;
        }
        written.insert(definition.name, resolved->image);
    }

    const QByteArray manifest = QJsonDocument(manifestFor(request)).toJson(QJsonDocument::Indented);
    if (!writeFile(directory.filePath(u"texturepack.json"_s), manifest) || !previewOf(written, request.resolution).save(directory.filePath(u"preview.png"_s), "PNG")) {
        QcHeadlessOptions::printError(u"Cannot write into "_s + request.directory);
        return 1;
    }
    std::println("Exported {} textures at {}x to {}", written.size(), request.resolution, request.directory.toStdString());
    return 0;
}

int printAtlasStats(const QcContentRegistry &content, const QString &modsDirectory)
{
    const QList<QcTexturePackLibrary::Entry> entries = QcTexturePackLibrary::scan(QcTexturePackLibrary::roots(modsDirectory));
    QStringList ids = {QString()};
    for (const QcTexturePackLibrary::Entry &entry : entries) {
        if (entry.origin == QcTexturePack::Origin::BuiltIn) {
            ids.append(entry.id);
        }
    }

    std::println("ATLAS pack requested tile extent levels MiB load_ms build_ms tiles_from_pack");
    for (const QString &id : std::as_const(ids)) {
        QElapsedTimer loading;
        loading.start();
        const QcTexturePackStack packs = id.isEmpty() ? QcTexturePackStack() : QcTexturePackLibrary::loadStack({id}, entries).stack;
        const qint64 loadMs = loading.elapsed();

        qint64 best = std::numeric_limits<qint64>::max();
        QcTextureAtlas atlas;
        for (int run = 0; run < 3; ++run) {
            QElapsedTimer timer;
            timer.start();
            atlas = QcTextureAtlas::build(content, packs);
            best = std::min(best, timer.elapsed());
        }
        std::println("ATLAS {} {} {} {}x{} {} {:.1f} {} {} {}", id.isEmpty() ? "base" : id.toStdString(), atlas.requestedTileSize(), atlas.tileSize(),
                     atlas.image().width(), atlas.image().height(), atlas.levels().size(), double(atlas.bytes()) / (1024.0 * 1024.0), loadMs,
                     best, id.isEmpty() ? 0 : atlas.sources().value(id));
    }
    return 0;
}

} // namespace QcTextureTools
