#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"

#include <QImage>
#include <QList>
#include <QRectF>
#include <QSet>
#include <QStringList>

class QcContentRegistry;
class QcTextureAtlas;

// The material maps of the texture atlas (decision E-908): a normal atlas and a specular atlas in
// the labPBR encoding (QcMaterialSynthesizer), with the colour atlas's layout, so the block shader
// reads them at the same atlas coordinates. Tiles are at most maxTileSize pixels (a 256x pack gets
// 128x maps). Each texture's maps come from where its colour came from: a pack's PNG brings its
// `<name>_n.png` and `<name>_s.png` (flat without them), a procedural texture (the game's, a mod's,
// a pack's spec, a procedural pack) gets maps painted from its colour (QcMaterialRules), everything
// else flat maps. Immutable once built; shared between threads like the colour atlas.
class QcMaterialAtlas
{
public:
    static constexpr int maxTileSize = 128;

    // The maps for every tile of `colour` (built from `content` and `packs`).
    static QcMaterialAtlas build(const QcTextureAtlas &colour, const QcContentRegistry &content, const QcTexturePackStack &packs);

    // Level 0 first, down to tiles of 8 pixels like the colour atlas (QcAtlasLayout::levelCount):
    // normals averaged and normalised again, smoothness lowered where the averaged normals spread
    // (Toksvig), emission averaged as light.
    [[nodiscard]] const QList<QImage> &normalLevels() const { return m_normal; }
    [[nodiscard]] const QList<QImage> &specularLevels() const { return m_specular; }
    [[nodiscard]] int tileSize() const { return m_tileSize; }
    // Bytes of all levels of both atlases.
    [[nodiscard]] qint64 bytes() const;
    // The tile at `uv` (QcTextureAtlas::uv) of level 0 of `levels`, for tests and tools.
    [[nodiscard]] QImage tile(const QList<QImage> &levels, const QRectF &uv) const;
    // Counts the sets of maps built in this process (1 for the first): the renderer remembers a
    // refused set by it, not by its address, which a later set may reuse.
    [[nodiscard]] quint64 serial() const { return m_serial; }
    // The pack images taken as material maps whose names are also among the content's texture
    // `names` (`foo_n.png` is the normal map of `foo`, never the texture `foo_n`; English, for the log).
    [[nodiscard]] static QStringList clashes(const QSet<QString> &names, const QcTexturePackStack &packs);

private:
    QList<QImage> m_normal;
    QList<QImage> m_specular;
    int m_tileSize = 16;
    quint64 m_serial = 0;
};
