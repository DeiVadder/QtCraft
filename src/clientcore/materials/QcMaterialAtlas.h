// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"

#include <QImage>
#include <QList>
#include <QRectF>
#include <QSet>
#include <QStringList>

class QcContentRegistry;
class QcTextureAtlas;

/*! \brief The material maps of the texture atlas (decision E-908): a normal atlas and a specular
    atlas in the labPBR encoding (QcMaterialSynthesizer), with the colour atlas's layout, so the
    block shader reads them at the same atlas coordinates.

    Tiles are at most maxTileSize pixels (a 256x pack gets 128x maps). Each texture's maps come
    from where its colour came from: a pack's PNG brings its \c {<name>_n.png} and
    \c {<name>_s.png} (flat without them), a procedural texture (the game's, a mod's, a pack's
    spec, a procedural pack) gets maps painted from its colour (QcMaterialRules), everything else
    flat maps. Immutable once built and handed from the worker that builds it to the renderer,
    which lets it go once the maps are on the GPU; never kept with the colour atlas (decision
    E-912). */
class QcMaterialAtlas
{
public:
    /*! Largest tile size of the maps in pixels. */
    static constexpr int maxTileSize = 128;

    /*! Returns the maps for every tile of \a colour (built from \a content and \a packs). */
    static QcMaterialAtlas build(const QcTextureAtlas &colour, const QcContentRegistry &content, const QcTexturePackStack &packs);

    /*! Returns the levels of the normal atlas: level 0 first, down to tiles of 8 pixels like the
        colour atlas (QcAtlasLayout::levelCount): normals averaged and normalised again,
        smoothness lowered where the averaged normals spread (Toksvig), emission averaged as
        light. */
    [[nodiscard]] const QList<QImage> &normalLevels() const { return m_normal; }
    /*! Returns the levels of the specular atlas, like normalLevels(). */
    [[nodiscard]] const QList<QImage> &specularLevels() const { return m_specular; }
    /*! Returns the tile size of level 0 in pixels. */
    [[nodiscard]] int tileSize() const { return m_tileSize; }
    /*! Returns the bytes of all levels of both atlases. */
    [[nodiscard]] qint64 bytes() const;
    /*! Returns the tile at \a uv (QcTextureAtlas::uv) of level 0 of \a levels, for tests and
        tools. */
    [[nodiscard]] QImage tile(const QList<QImage> &levels, const QRectF &uv) const;
    /*! Returns the number of this set among the sets of maps built in this process (1 for the
        first): the renderer remembers a refused set by it, not by its address, which a later set
        may reuse. */
    [[nodiscard]] quint64 serial() const { return m_serial; }
    /*! Returns the images of \a packs taken as material maps whose names are also among the
        content's texture \a names (\c {foo_n.png} is the normal map of \c {foo}, never the
        texture \c {foo_n}; English, for the log). */
    [[nodiscard]] static QStringList clashes(const QSet<QString> &names, const QcTexturePackStack &packs);

private:
    QList<QImage> m_normal;
    QList<QImage> m_specular;
    int m_tileSize = 16;
    quint64 m_serial = 0;
};
