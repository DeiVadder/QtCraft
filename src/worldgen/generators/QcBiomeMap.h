// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <optional>

class QcTerrainGenerator;

/*! \brief A top-down map of a terrain world (decision E-921): biome, height and water of one column
    per cell, each exactly the column QcTerrainGenerator::column() computes at the cell's north-west
    corner (the same call that decides the biomes of the game), without generating chunks.

    The map preview of a new world and qtcraft-headless --biome-map draw it; sites() adds the
    villages and structures the generator plans there. */
class QcBiomeMap
{
public:
    /*! \brief The square of columns a map shows: \c {radius} blocks around the centre, one cell
        per \c {blocksPerCell} blocks. */
    struct Area
    {
        /*! The x, z of the centre column. \qcshared */
        int centreX = 0;
        int centreZ = 0;
        /*! Half the side of the square in blocks. */
        int radius = 0;
        /*! The side of one cell in blocks. */
        int blocksPerCell = 1;

        bool operator==(const Area &) const = default;
        /*! Returns the number of cells along each side. */
        [[nodiscard]] int cells() const { return 2 * radius / blocksPerCell; }
        /*! Returns the world x of the north-west corner of the cells in column \a cell. */
        [[nodiscard]] int blockX(int cell) const { return centreX - radius + cell * blocksPerCell; }
        /*! Returns the world z of the north-west corner of the cells in row \a cell. */
        [[nodiscard]] int blockZ(int cell) const { return centreZ - radius + cell * blocksPerCell; }
    };

    /*! \brief What covers a column: land, still water (the sea and lakes under the sea level) or
        the water of a river channel (a river biome, decision E-840). */
    enum class Water : quint8 {
        Land, // No water on the column.
        Sea, // Water up to the sea level: the sea and basins below it, not surface lakes.
        River // The water of a river channel.
    };

    /*! \brief What the map shows of one column. */
    struct Cell
    {
        /*! The column's biome (a river biome in a river channel; under the sea the land biome of
            the climate). */
        int biome = 0;
        /*! The height of the terrain surface (the top solid block is one below). */
        int height = 0;
        /*! What covers the column. */
        Water water = Water::Land;
        /*! The top of the water is ice (QcTerrainGenerator::frozenWaterAt, as the chunks get
            it). */
        bool frozen = false;
    };

    /*! \brief A village or structure on the map: its kind and the column it stands at. */
    struct Site
    {
        /*! \brief Whether a site is a village or a structure. */
        enum class Kind : quint8 {
            Village, // A village (its centre).
            Structure // A surface structure or a coastal tower.
        };

        /*! Whether the site is a village or a structure. */
        Kind kind = Kind::Village;
        /*! The x, z of the column the site stands at: a village's centre, the middle of a
            structure's footprint or a coastal tower's doorstep. \qcshared */
        int x = 0;
        int z = 0;
        /*! The kind: the structure's name ("qtcraft:desert_temple"), "coastal_tower" or
            "village" (one of siteKinds()). */
        QString name;
    };

    /*! Asked between rows (and regions); true stops the work. */
    using Cancelled = std::function<bool()>;

    /*! Returns the map of \a area with the cells \a generator computes; none when \a cancelled
        said so first. */
    [[nodiscard]] static std::optional<QcBiomeMap> sample(const QcTerrainGenerator &generator, Area area, const Cancelled &cancelled = {});
    /*! Returns the cell \a generator computes for the column \a x, \a z. */
    [[nodiscard]] static Cell cellAt(const QcTerrainGenerator &generator, int x, int z);
    /*! Returns the villages, surface structures and coastal towers whose place lies in \a area,
        as \a generator plans them (this plans their regions, which costs far more than the
        cells); none when \a cancelled said so first. Mine shafts and dungeons (underground) are
        left out: in a debug build their regions over 2048 × 2048 blocks take about 4 s and 3 s
        to plan (decision E-921). */
    [[nodiscard]] static std::optional<QList<Site>> sites(const QcTerrainGenerator &generator, Area area, const Cancelled &cancelled = {});
    /*! Returns the kinds sites() can find with \a generator, in a fixed order: "village",
        "coastal_tower", then the surface structures by name (QcSurfacePlanner::kinds), so a kind
        keeps its place (and its marker on the map) whatever the world's options turn off. */
    [[nodiscard]] static QStringList siteKinds(const QcTerrainGenerator &generator);

    /*! Returns the area the map covers. */
    [[nodiscard]] Area area() const { return m_area; }
    /*! Returns the number of cells along each side. */
    [[nodiscard]] int size() const { return m_area.cells(); }
    /*! Returns the cell in column \a x and row \a z of the map (counted from the north-west
        corner, not world coordinates). */
    [[nodiscard]] const Cell &cell(int x, int z) const { return m_cells.at(static_cast<qsizetype>(z) * size() + x); }
    /*! Returns how many cells of land and river each of the first \a biomeCount biomes has (index:
        biome); the sea counts for none. */
    [[nodiscard]] QList<int> biomeCells(int biomeCount) const;

private:
    explicit QcBiomeMap(Area area);

    Area m_area;
    QList<Cell> m_cells;
};
