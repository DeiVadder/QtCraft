#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"
#include "clientcore/texturepacks/QcTexturePackProblem.h"
#include "clientcore/textures/QcAtlasLayout.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QRectF>

struct QcTextureAtlasOptions
{
    // Largest texture edge the GPU takes (QRhi::TextureSizeMax; 8192 until the renderer knows).
    int maxExtent = 8192;
    // Bytes of all mip levels together.
    qint64 memoryBudget = qint64(256) * 1024 * 1024;
    // Largest tile size used: textures (QcTexturePackLoader::Limits::maxStoredResolution); smaller
    // for atlases that only feed icons.
    int maxTileSize = 256;
    // Threads that draw the tiles; 0: half the cores, at most 8.
    int threads = 0;
};

// All content textures in one image with its mip levels (decision E-300). Each texture comes from
// the active texture packs or the content (QcTextureSources); unknown or broken textures map to a
// magenta placeholder. The tile size is the highest resolution of the packs (16 without packs),
// halved until the atlas fits the GPU's texture size and the memory budget. The UV rectangles
// depend only on the list of textures, not on the tile size (QcAtlasLayout).
class QcTextureAtlas
{
public:
    static constexpr auto missingTextureName = "qtcraft:missing";

    using Options = QcTextureAtlasOptions;

    static QcTextureAtlas build(const QcContentRegistry &content, const QcTexturePackStack &packs = {}, const Options &options = {});

    // Mip level 0.
    const QImage &image() const { return m_levels.front(); }
    // Level 0 first, each level half the size of the one before.
    const QList<QImage> &levels() const { return m_levels; }
    int tileSize() const { return m_tileSize; }
    // The resolution the packs asked for (tileSize() is smaller when a limit applied).
    int requestedTileSize() const { return m_requestedTileSize; }
    // Bytes of all levels.
    qint64 bytes() const;

    // UV rectangle in [0, 1]; falls back to the placeholder for unknown names.
    QRectF uv(const QString &textureName) const;
    bool contains(const QString &textureName) const { return m_uvs.contains(textureName); }
    bool sameLayout(const QcTextureAtlas &other) const { return m_uvs == other.m_uvs; }
    // Single texture tile from the level whose tiles are at most `maxSide` pixels (0: level 0),
    // e.g. for item icons in the UI.
    QImage tile(const QString &textureName, int maxSide = 0) const;
    // The level whose tiles are at most `maxSide` pixels (at least the last level).
    const QImage &levelForTile(int maxSide) const;

    // Content problems (English, for the log).
    QStringList problems() const { return m_problems; }
    // For the player: the atlas uses a lower resolution than the packs asked for.
    QcTexturePackProblems notices() const { return m_notices; }
    // How many tiles each source gave ("" = the content itself).
    const QHash<QString, int> &sources() const { return m_sources; }

private:
    int tileSizeFor(QcAtlasLayout layout, const Options &options);

    QList<QImage> m_levels = {QImage()};
    int m_tileSize = 16;
    int m_requestedTileSize = 16;
    QHash<QString, QRectF> m_uvs;
    QStringList m_problems;
    QcTexturePackProblems m_notices;
    QHash<QString, int> m_sources;
};
