#include "clientcore/textures/QcTextureAtlas.h"

#include "clientcore/texturepacks/QcTexturePackLoader.h"
#include "clientcore/texturepacks/QcTextureSources.h"
#include "clientcore/textures/QcAlphaCoverage.h"
#include "clientcore/textures/QcPixelFont.h"
#include "clientcore/textures/QcTextureSynthesizer.h"

#include <QSet>
#include <QThread>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <memory>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

constexpr int baseTileSize = QcTextureSynthesizer::tileSize;

struct Tile
{
    std::optional<QcTextureSources::Resolved> resolved;
    QString problem;
};

QImage missingTile(int size)
{
    QImage image(size, size, QImage::Format_RGBA8888);
    const int square = size / 2;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            image.setPixelColor(x, y, ((x / square) + (y / square)) % 2 == 0 ? QColor(255, 0, 255) : QColor(0, 0, 0));
        }
    }
    return image;
}

// How many threads draw the tiles: half the cores, at most maxDrawThreads (the build may itself run
// in a worker thread next to the game).
qsizetype drawThreads(int requested, qsizetype tiles)
{
    constexpr int maxDrawThreads = 8;
    const int automatic = std::clamp(QThread::idealThreadCount() / 2, 1, maxDrawThreads);
    return std::clamp<qsizetype>(requested > 0 ? requested : automatic, 1, std::max<qsizetype>(1, tiles));
}

// Every tile is drawn on its own from immutable input, so the tiles are drawn in parallel: one
// short-lived QThread per share of the tiles, each writing only its own elements of the
// preallocated result. A thread that cannot be started leaves its share to the calling thread.
// Each share ends with a release store that the caller reads with acquire after QThread::wait(),
// so ThreadSanitizer sees the hand-over (it does not see Qt's own waiting on macOS, nor QtConcurrent's).
std::vector<Tile> drawTiles(const QList<QcTextureDefinition> &definitions, const QcTexturePackStack &packs, int tileSize, int threads)
{
    std::vector<Tile> tiles(static_cast<std::size_t>(definitions.size()));
    const qsizetype jobs = drawThreads(threads, definitions.size());
    std::vector<std::atomic<bool>> done(static_cast<std::size_t>(jobs));
    const auto draw = [&definitions, &packs, &tiles, &done, tileSize, jobs](qsizetype first) {
        for (qsizetype i = first; i < definitions.size(); i += jobs) {
            Tile &tile = tiles[static_cast<std::size_t>(i)];
            QcResult<QcTextureSources::Resolved> resolved = QcTextureSources::resolve(definitions[i], packs, tileSize);
            if (!resolved) {
                tile.problem = resolved.error();
                continue;
            }
            QcAlphaCoverage::fillTransparentColour(resolved->image);
            tile.resolved = std::move(*resolved);
        }
        done[static_cast<std::size_t>(first)].store(true, std::memory_order_release);
    };

    std::vector<std::unique_ptr<QThread>> helpers;
    for (qsizetype job = 1; job < jobs; ++job) {
        std::unique_ptr<QThread> helper(QThread::create(draw, job));
        helper->start();
        if (!helper->isRunning() && !helper->isFinished()) {
            draw(job);
            continue;
        }
        helpers.push_back(std::move(helper));
    }
    draw(0);
    for (const std::unique_ptr<QThread> &helper : helpers) {
        helper->wait();
    }
    for (const std::atomic<bool> &finished : done) {
        Q_ASSERT(finished.load(std::memory_order_acquire));
    }
    return tiles;
}

} // namespace

int QcTextureAtlas::tileSizeFor(QcAtlasLayout layout, const Options &options)
{
    int size = std::min(m_requestedTileSize, std::max(baseTileSize, options.maxTileSize));
    while (size > baseTileSize) {
        const QSize extent = layout.extent(size);
        if (std::max(extent.width(), extent.height()) <= options.maxExtent && layout.bytes(size) <= options.memoryBudget) {
            break;
        }
        size /= 2;
    }

    if (size != m_requestedTileSize && options.maxTileSize >= QcTexturePackLoader::Limits{}.maxStoredResolution) {
        m_notices.append({{},
                          QT_TRANSLATE_NOOP("QcTexturePack",
                                            "Textures are shown at %1x instead of %2x: the game uses at most 256x, and the atlas must fit "
                                            "the GPU texture size and the memory budget"),
                          {QString::number(size), QString::number(m_requestedTileSize)}});
    }
    return size;
}

QcTextureAtlas QcTextureAtlas::build(const QcContentRegistry &content, const QcTexturePackStack &packs, const Options &options)
{
    QcTextureAtlas atlas;
    const QList<QcTextureDefinition> &definitions = content.assets().textures();
    // The placeholder, the content's textures and, for content with signs, the pages of the sign
    // font (decision E-766).
    const bool signs = std::ranges::any_of(content.blocks(), [](const QcBlockDefinition &block) { return block.sign.isSign(); });
    const int glyphPages = signs ? QcPixelFont::pageCount() : 0;
    const QcAtlasLayout layout(definitions.size() + 1 + glyphPages);
    atlas.m_requestedTileSize = QcTextureSources::resolutionOf(packs, QcTexturePackManifest::maxResolution);
    atlas.m_tileSize = atlas.tileSizeFor(layout, options);

    const QSize extent = layout.extent(atlas.m_tileSize);
    QImage level(extent, QImage::Format_RGBA8888);
    level.fill(Qt::transparent);
    layout.place(level, 0, missingTile(atlas.m_tileSize));
    atlas.m_uvs.insert(QString::fromLatin1(missingTextureName), layout.uv(0));

    const std::vector<Tile> tiles = drawTiles(definitions, packs, atlas.m_tileSize, options.threads);

    qsizetype index = 1;
    for (qsizetype i = 0; i < definitions.size(); ++i) {
        const QcTextureDefinition &definition = definitions[i];
        const std::optional<QcTextureSources::Resolved> &resolved = tiles[static_cast<std::size_t>(i)].resolved;
        if (!resolved) {
            atlas.m_problems.append(tiles[static_cast<std::size_t>(i)].problem);
            continue;
        }
        layout.place(level, index, resolved->image);
        atlas.m_uvs.insert(definition.name, layout.uv(index));
        ++atlas.m_sources[resolved->pack];
        ++index;
    }
    for (int page = 0; page < glyphPages; ++page) {
        QImage glyphs = QcPixelFont::page(page, atlas.m_tileSize);
        QcAlphaCoverage::fillTransparentColour(glyphs);
        layout.place(level, index, glyphs);
        atlas.m_uvs.insert(QcPixelFont::pageName(page), layout.uv(index));
        ++index;
    }

    const QList<double> coverage = QcAlphaCoverage::measure(level, layout, index, atlas.m_tileSize);
    atlas.m_levels = {level};
    for (int next = 1; next < QcAtlasLayout::levelCount(atlas.m_tileSize); ++next) {
        QImage smaller = QcAtlasLayout::halve(atlas.m_levels.back());
        QcAlphaCoverage::keep(smaller, layout, coverage, atlas.m_tileSize >> next);
        atlas.m_levels.append(std::move(smaller));
    }

    for (const QcBlockDefinition &block : content.blocks()) {
        for (const QString &texture : block.faceTextures) {
            if (!texture.isEmpty() && !atlas.m_uvs.contains(texture)) {
                atlas.m_problems.append(u"Block %1 uses unknown texture %2"_s.arg(block.name, texture));
            }
        }
    }
    atlas.m_problems.append(atlas.transparentTexelsOfOpaqueBlocks(content));
    atlas.m_problems.removeDuplicates();
    return atlas;
}

QStringList QcTextureAtlas::transparentTexelsOfOpaqueBlocks(const QcContentRegistry &content) const
{
    QStringList problems;
    QSet<QString> checked;
    for (const QcBlockDefinition &block : content.blocks()) {
        if (block.renderLayer != QcRenderLayer::Opaque) {
            continue;
        }
        QStringList textures(block.faceTextures.begin(), block.faceTextures.end());
        for (const auto &[slot, texture] : block.textureSlots) {
            textures.append(texture);
        }
        for (const QString &texture : textures) {
            if (texture.isEmpty() || !m_uvs.contains(texture) || checked.contains(texture)) {
                continue;
            }
            checked.insert(texture);
            if (QcAlphaCoverage::hasTexelBelowCutoff(tile(texture))) {
                problems.append(u"Block %1 of the opaque layer uses texture %2 with transparent texels; they are drawn in their colour (render it as cutout)"_s
                                    .arg(block.name, texture));
            }
        }
    }
    return problems;
}

qint64 QcTextureAtlas::bytes() const
{
    qint64 total = 0;
    for (const QImage &level : m_levels) {
        total += level.sizeInBytes();
    }
    return total;
}

QRectF QcTextureAtlas::uv(const QString &textureName) const
{
    const auto it = m_uvs.constFind(textureName);
    return it != m_uvs.constEnd() ? *it : m_uvs.value(QString::fromLatin1(missingTextureName));
}

const QImage &QcTextureAtlas::levelForTile(int maxSide) const
{
    qsizetype level = 0;
    int size = m_tileSize;
    while (maxSide > 0 && size > maxSide && level + 1 < m_levels.size()) {
        size /= 2;
        ++level;
    }
    return m_levels[level];
}

QImage QcTextureAtlas::tile(const QString &textureName, int maxSide) const
{
    const QImage &level = levelForTile(maxSide);
    const int size = m_tileSize * level.width() / std::max(1, image().width());
    const QRectF rect = uv(textureName);
    const int x = static_cast<int>(std::lround(rect.x() * level.width()));
    const int y = static_cast<int>(std::lround(rect.y() * level.height()));
    return level.copy(x, y, size, size);
}
