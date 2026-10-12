#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QSize>

// Geometry of the texture atlas (decision E-300). Tiles sit in a grid of cells; a cell is the tile
// plus a gutter of tile / 8 pixels on every side that repeats the tile's edge pixels, so filtering
// and mip levels never mix neighbouring tiles. Measured in eighths of a tile (a cell is 10, the
// gutter 1, the tile 8), every UV rectangle is the same at every tile size: changing the
// resolution re-uploads the atlas but keeps all meshes.
class QcAtlasLayout
{
public:
    QcAtlasLayout() = default;
    explicit QcAtlasLayout(qsizetype tiles);

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    // UV rectangle (0..1) of tile `index`, independent of the tile size.
    QRectF uv(qsizetype index) const;

    static int gutter(int tileSize) { return tileSize / 8; }
    static int cellSize(int tileSize) { return tileSize + 2 * gutter(tileSize); }
    // Mip levels down to tiles of 8 pixels (16: 2, 32: 3, 64: 4, …, 256: 6), each with a gutter of
    // at least one pixel.
    static int levelCount(int tileSize);
    QSize extent(int tileSize) const { return {m_columns * cellSize(tileSize), m_rows * cellSize(tileSize)}; }
    // Bytes of all mip levels at `tileSize` (RGBA, 4 bytes per pixel).
    qint64 bytes(int tileSize) const;
    // Top left pixel of tile `index` (inside its gutter) at `tileSize`.
    QPoint tileOrigin(qsizetype index, int tileSize) const;

    // Draws `tile` (tileSize x tileSize RGBA8888) into its cell of `atlas` and fills the gutter
    // around it with the tile's edge pixels.
    void place(QImage &atlas, qsizetype index, const QImage &tile) const;
    // The next mip level: every 2 x 2 block averaged, colours weighted by their alpha so
    // transparent pixels do not darken the edges of cut-out textures.
    static QImage halve(const QImage &level);
    // Index of the first of `levels` whose width and height are at most `maxSize`; -1 if none is.
    static qsizetype firstLevelWithin(const QList<QImage> &levels, int maxSize);

private:
    int m_columns = 1;
    int m_rows = 1;
};
