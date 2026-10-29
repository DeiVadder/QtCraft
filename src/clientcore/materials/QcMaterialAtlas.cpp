// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/materials/QcMaterialAtlas.h"

#include "clientcore/materials/QcMaterialMips.h"
#include "clientcore/materials/QcMaterialRules.h"
#include "clientcore/materials/QcMaterialSynthesizer.h"
#include "clientcore/textures/QcAtlasLayout.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QSet>

#include <atomic>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The texture names a block with a light level uses: their bright texels glow.
QSet<QString> lightTextures(const QcContentRegistry &content)
{
    QSet<QString> names;
    for (const QcBlockDefinition &block : content.blocks()) {
        if (block.lightLevel <= 0) {
            continue;
        }
        for (const QString &texture : block.faceTextures) {
            names.insert(texture);
        }
        for (const auto &[slot, texture] : block.textureSlots) {
            names.insert(texture);
        }
    }
    return names;
}

QColor colourOf(const QJsonObject &definition, const char *key)
{
    return QColor::fromString(definition.value(QLatin1StringView(key)).toString());
}

// What the maps of one tile are painted from.
struct TileInput
{
    const QcContentRegistry &content;
    const QcTexturePackStack &packs;
    const QSet<QString> &glowing;
    int size = 16;
};

QcMaterialSynthesizer::Maps procedural(const QString &name, const QJsonObject &definition, const QImage &colour, const TileInput &input)
{
    const QcMaterialParams params = QcMaterialRules::forTexture(name, definition, input.glowing.contains(name));
    return QcMaterialSynthesizer::paint(colour, params, {colourOf(definition, "primary"), colourOf(definition, "secondary")});
}

// A pack's image maps at the tile size (box-filtered, decision E-908); the flat map for a missing one.
QcMaterialSynthesizer::Maps fromPack(const QcTexturePack &pack, const QString &name, int size)
{
    QcMaterialSynthesizer::Maps maps = QcMaterialSynthesizer::flat(size);
    const QImage normal = pack.normalMaps.value(name);
    const QImage specular = pack.specularMaps.value(name);
    if (!normal.isNull()) {
        maps.normal = QcMaterialMips::normalAt(normal, size);
    }
    if (!specular.isNull()) {
        maps.specular = QcMaterialMips::specularAt(QcMaterialSynthesizer::packEmission(specular), size);
    }
    return maps;
}

// The maps of a tile whose colour came from `source` (a pack id, "" for the content).
QcMaterialSynthesizer::Maps mapsOf(const QcTextureAtlas::TileSource &source, const QImage &colour, const TileInput &input)
{
    const QList<QcTextureDefinition> &definitions = input.content.assets().textures();
    if (source.definition < 0 || source.definition >= definitions.size()) {
        return QcMaterialSynthesizer::flat(input.size);
    }
    const QcTextureDefinition &definition = definitions[source.definition];
    if (!source.pack.isEmpty()) {
        const auto pack = std::ranges::find_if(input.packs, [&source](const QcTexturePackRef &candidate) { return candidate->id == source.pack; });
        if (pack != input.packs.end()) {
            if ((*pack)->images.contains(definition.name)) {
                return fromPack(**pack, definition.name, input.size);
            }
            if ((*pack)->specs.contains(definition.name)) {
                return procedural(definition.name, (*pack)->specs.value(definition.name), colour, input);
            }
        }
    }
    if (!definition.png.isEmpty() || definition.procedural.isEmpty()) {
        return QcMaterialSynthesizer::flat(input.size);
    }
    return procedural(definition.name, definition.procedural, colour, input);
}

} // namespace

QcMaterialAtlas QcMaterialAtlas::build(const QcTextureAtlas &colour, const QcContentRegistry &content, const QcTexturePackStack &packs)
{
    // Counts every set of maps built in this process (serial()); atomic: atlases are built in workers.
    static std::atomic<quint64> builds{0};
    QcMaterialAtlas atlas;
    atlas.m_serial = builds.fetch_add(1, std::memory_order_relaxed) + 1;
    atlas.m_tileSize = std::min(colour.tileSize(), maxTileSize);
    const QcAtlasLayout &layout = colour.layout();
    const QImage &colourLevel = colour.levelForTile(atlas.m_tileSize);
    const QSet<QString> glowing = lightTextures(content);
    const TileInput input{content, packs, glowing, atlas.m_tileSize};

    const QSize extent = layout.extent(atlas.m_tileSize);
    QImage normal(extent, QImage::Format_RGBA8888);
    QImage specular(extent, QImage::Format_RGBA8888);
    const QcMaterialSynthesizer::Maps flat = QcMaterialSynthesizer::flat(1);
    normal.fill(flat.normal.pixelColor(0, 0));
    specular.fill(flat.specular.pixelColor(0, 0));

    const QList<QcTextureAtlas::TileSource> &sources = colour.tileSources();
    for (qsizetype index = 0; index < sources.size(); ++index) {
        const QPoint origin = layout.tileOrigin(index, atlas.m_tileSize);
        const QImage tile = colourLevel.copy(origin.x(), origin.y(), atlas.m_tileSize, atlas.m_tileSize);
        const QcMaterialSynthesizer::Maps maps = mapsOf(sources[index], tile, input);
        layout.place(normal, index, maps.normal);
        layout.place(specular, index, maps.specular);
    }

    atlas.m_normal = {normal};
    atlas.m_specular = {specular};
    for (int next = 1; next < QcAtlasLayout::levelCount(atlas.m_tileSize); ++next) {
        QcMaterialMips::Level level = QcMaterialMips::halve(atlas.m_normal.back(), atlas.m_specular.back());
        atlas.m_normal.append(std::move(level.normal));
        atlas.m_specular.append(std::move(level.specular));
    }
    return atlas;
}

namespace {

// The maps of `pack` in `maps` (with their file `suffix`) whose file names are textures in `names`.
QStringList clashesOf(const QcTexturePack &pack, const QHash<QString, QImage> &maps, QLatin1StringView suffix, const QSet<QString> &names)
{
    QStringList problems;
    for (auto it = maps.cbegin(); it != maps.cend(); ++it) {
        if (names.contains(it.key() + suffix)) {
            problems.append(u"Texture pack %1: %2%3.png is read as a material map of %2, not as the texture %2%3"_s.arg(pack.id, it.key(), suffix));
        }
    }
    return problems;
}

} // namespace

QStringList QcMaterialAtlas::clashes(const QSet<QString> &names, const QcTexturePackStack &packs)
{
    QStringList problems;
    for (const QcTexturePackRef &pack : packs) {
        problems.append(clashesOf(*pack, pack->normalMaps, "_n"_L1, names));
        problems.append(clashesOf(*pack, pack->specularMaps, "_s"_L1, names));
    }
    return problems;
}

qint64 QcMaterialAtlas::bytes() const
{
    qint64 total = 0;
    for (const QImage &level : m_normal) {
        total += level.sizeInBytes();
    }
    for (const QImage &level : m_specular) {
        total += level.sizeInBytes();
    }
    return total;
}

QImage QcMaterialAtlas::tile(const QList<QImage> &levels, const QRectF &uv) const
{
    if (levels.isEmpty()) {
        return {};
    }
    const QImage &level = levels.front();
    const int x = static_cast<int>(std::lround(uv.x() * level.width()));
    const int y = static_cast<int>(std::lround(uv.y() * level.height()));
    return level.copy(x, y, m_tileSize, m_tileSize);
}
