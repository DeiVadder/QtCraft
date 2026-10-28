#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"
#include "clientcore/texturepacks/QcTexturePackProblem.h"
#include "clientcore/textures/QcAtlasLayout.h"
#include "common/content/definitions/QcTextureAnimation.h"
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
    qint64 memoryBudget = qint64{256} * 1024 * 1024;
    // Largest tile size used: textures (QcTexturePackLoader::Limits::maxStoredResolution); smaller
    // for atlases that only feed icons.
    int maxTileSize = 256;
    // Threads that draw the tiles; 0: half the cores, at most 8.
    int threads = 0;
    // The material maps (QcMaterialAtlas, decision E-908) are built for this atlas: the packs'
    // map files are checked against the texture names (problems()). The maps themselves are built
    // apart from the atlas (QcTextureStackBuild) and never kept with it (decision E-912).
    bool materials = false;
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
    // Where the tile at an index of the layout came from (decision E-908): the index of its
    // definition in the content's textures (-1 for the placeholder and the tiles the game draws
    // itself) and the id of the pack that gave it ("" for the content). Later frames of an
    // animation name their texture's definition too.
    struct TileSource
    {
        qsizetype definition = -1;
        QString pack;
    };

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

    // The animated textures for the block shader (decision E-878): one RGBA8 texel per cell of the
    // atlas grid (columns x rows), set at the cell of an animated texture's first frame: red the
    // frame count plus 128 with interpolation, green the frame time in ticks, blue and alpha the
    // low and high byte of the tile index of its second frame (the later frames follow it, row by
    // row); 0 at every other cell. Each frame is a tile of its own with its gutter, so filtering and
    // mip levels never mix frames.
    const QImage &animationMap() const { return m_animationMap; }
    // For tests and tools (the renderer reads animationMap()): the animation of a texture (a still
    // one for unknown names) and the UV rectangle of its frame `frame` (frame 0 and still
    // textures: uv()).
    QcTextureAnimation animation(const QString &textureName) const;
    QRectF frameUv(const QString &textureName, int frame) const;
    // The UV rectangle a texture shows `ticks` into its animation: the frame the block shader picks
    // at that time (QcTextureAnimation::frameAt), without blending into the next one; uv() for
    // still textures. Entity meshes, built on the CPU every frame, pick their frames this way, so
    // flames on a burning body flicker like fire blocks (decision E-919).
    [[nodiscard]] QRectF uvAt(const QString &textureName, double ticks) const;
    // Ticks after which every animation of the atlas starts over (the least common multiple of
    // their loops): the time handed to the shader wraps there, so the float it gets keeps 1/8 tick
    // at most. Capped at maxCycleTicks (2^20 ticks, about 14.5 h): a larger multiple (loops of
    // many different prime lengths) wraps there anyway, and the animations whose loop does not
    // divide it jump once at that wrap.
    int animationCycleTicks() const;
    static constexpr int maxCycleTicks = 1 << 20;

    // The layout of the tiles (also of the material maps) and where each tile came from.
    const QcAtlasLayout &layout() const { return m_layout; }
    const QList<TileSource> &tileSources() const { return m_tileSources; }

    // Content problems (English, for the log).
    QStringList problems() const { return m_problems; }
    // For the player: the atlas uses a lower resolution than the packs asked for.
    QcTexturePackProblems notices() const { return m_notices; }
    // How many tiles each source gave ("" = the content itself).
    const QHash<QString, int> &sources() const { return m_sources; }

private:
    // An animated texture: its animation, and the tile indices of its first and second frame.
    struct Animated
    {
        QcTextureAnimation animation;
        qsizetype first = 0;
        qsizetype later = 0;
    };
    // A texture whose frames after the first are still to be placed.
    struct PendingFrames
    {
        QString name;
        qsizetype first = 0;
        QcTextureAnimation animation;
        // All frames the source gave, the first included.
        QList<QImage> frames;
    };

    int tileSizeFor(QcAtlasLayout layout, const Options &options);
    // Places the later frames of `pending` from tile `next` on and fills the animation map; a
    // texture whose source gave fewer frames than reserved repeats its last one. Returns the index
    // after the last frame placed.
    qsizetype placeLaterFrames(QImage &level, const QList<PendingFrames> &pending, qsizetype next);

    QList<QImage> m_levels = {QImage()};
    int m_tileSize = 16;
    int m_requestedTileSize = 16;
    QHash<QString, QRectF> m_uvs;
    QcAtlasLayout m_layout;
    QHash<QString, Animated> m_animated;
    QImage m_animationMap;
    // One problem per texture with a transparent texel that a block of the opaque layer uses: it
    // is drawn without alpha test (decision E-756).
    QStringList transparentTexelsOfOpaqueBlocks(const QcContentRegistry &content) const;
    // One problem per face texture of a block that the atlas does not have.
    QStringList unknownTextures(const QcContentRegistry &content) const;

    QList<TileSource> m_tileSources;
    QStringList m_problems;
    QcTexturePackProblems m_notices;
    QHash<QString, int> m_sources;
};
