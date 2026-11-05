// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QSize>

/*! \brief Geometry of the texture atlas (decision E-300).

    Tiles sit in a grid of cells; a cell is the tile plus a gutter of tile / 8 pixels on every
    side that repeats the tile's edge pixels, so filtering and mip levels never mix neighbouring
    tiles. Measured in eighths of a tile (a cell is 10, the gutter 1, the tile 8), every UV
    rectangle is the same at every tile size: changing the resolution re-uploads the atlas but
    keeps all meshes. */
class QcAtlasLayout
{
public:
    QcAtlasLayout() = default;
    explicit QcAtlasLayout(qsizetype tiles);

    /*! Returns the number of tile columns. */
    int columns() const { return m_columns; }
    /*! Returns the number of tile rows. */
    int rows() const { return m_rows; }
    /*! Returns the UV rectangle (0..1) of tile \a index, independent of the tile size. */
    QRectF uv(qsizetype index) const;

    /*! Tile size of the last mip level (levelCount()). */
    static constexpr int minimumMipTile = 8;

    /*! Returns the gutter width in pixels at \a tileSize (tileSize / 8). */
    static int gutter(int tileSize) { return tileSize / 8; }
    /*! Returns the edge of a cell in pixels at \a tileSize: the tile plus a gutter on both sides.
     */
    static int cellSize(int tileSize) { return tileSize + 2 * gutter(tileSize); }
    /*! Returns the number of mip levels at \a tileSize down to tiles of 8 pixels (16: 2, 32: 3,
        64: 4, …, 256: 6), each with a gutter of at least one pixel. */
    static int levelCount(int tileSize);
    /*! Returns the columns and rows of the atlas whose last level (levelCount(), tiles of 8
        pixels) is \a lastLevel pixels large; at least 1 x 1. */
    static QSize gridOf(QSize lastLevel);
    /*! Returns the size in pixels of mip level 0 at \a tileSize. */
    QSize extent(int tileSize) const { return {m_columns * cellSize(tileSize), m_rows * cellSize(tileSize)}; }
    /*! Returns the bytes of all mip levels at \a tileSize (RGBA, 4 bytes per pixel). */
    qint64 bytes(int tileSize) const;
    /*! Returns the top left pixel of tile \a index (inside its gutter) at \a tileSize. */
    QPoint tileOrigin(qsizetype index, int tileSize) const;

    /*! Draws \a tile (tileSize x tileSize RGBA8888) into the cell of tile \a index in \a atlas
        and fills the gutter around it with the tile's edge pixels. */
    void place(QImage &atlas, qsizetype index, const QImage &tile) const;
    /*! Returns the mip level after \a level: every 2 x 2 block averaged, colours weighted by
        their alpha so transparent pixels do not darken the edges of cut-out textures. */
    static QImage halve(const QImage &level);
    /*! Returns the index of the first of \a levels whose width and height are at most
        \a maxSize; -1 if none is. */
    static qsizetype firstLevelWithin(const QList<QImage> &levels, int maxSize);

private:
    int m_columns = 1;
    int m_rows = 1;
};
