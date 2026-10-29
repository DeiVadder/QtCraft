// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <optional>

class QcTerrainGenerator;

// A top-down map of a terrain world (decision E-921): biome, height and water of one column per
// cell, each exactly the column QcTerrainGenerator::column() computes at the cell's north-west
// corner (the same call that decides the biomes of the game), without generating chunks. The map
// preview of a new world and qtcraft-headless --biome-map draw it; sites() adds the villages and
// structures the generator plans there.
class QcBiomeMap
{
public:
    // The square of columns a map shows: `radius` blocks around the centre, one cell per
    // `blocksPerCell` blocks.
    struct Area
    {
        int centreX = 0;
        int centreZ = 0;
        int radius = 0;
        int blocksPerCell = 1;

        bool operator==(const Area &) const = default;
        // Cells along each side.
        [[nodiscard]] int cells() const { return 2 * radius / blocksPerCell; }
        // The world column of a cell's north-west corner.
        [[nodiscard]] int blockX(int cell) const { return centreX - radius + cell * blocksPerCell; }
        [[nodiscard]] int blockZ(int cell) const { return centreZ - radius + cell * blocksPerCell; }
    };

    // What covers a column: land, still water (the sea and lakes under the sea level) or the water
    // of a river channel (a river biome, decision E-840).
    enum class Water : quint8 { Land, Sea, River };

    struct Cell
    {
        // The column's biome (a river biome in a river channel; under the sea the land biome of
        // the climate).
        int biome = 0;
        // The height of the terrain surface (the top solid block is one below).
        int height = 0;
        Water water = Water::Land;
        // The top of the water is ice (QcTerrainGenerator::frozenWaterAt, as the chunks get it).
        bool frozen = false;
    };

    // A village or structure on the map: its kind and the column it stands at.
    struct Site
    {
        enum class Kind : quint8 { Village, Structure };

        Kind kind = Kind::Village;
        int x = 0;
        int z = 0;
        // The kind: the structure's name ("qtcraft:desert_temple"), "coastal_tower" or "village" (one
        // of siteKinds()).
        QString name;
    };

    // Asked between rows (and regions); true stops the work.
    using Cancelled = std::function<bool()>;

    // The cells of `area`; none when `cancelled` said so first.
    [[nodiscard]] static std::optional<QcBiomeMap> sample(const QcTerrainGenerator &generator, Area area, const Cancelled &cancelled = {});
    // The cell of the column x, z.
    [[nodiscard]] static Cell cellAt(const QcTerrainGenerator &generator, int x, int z);
    // The villages, surface structures and coastal towers whose place lies in `area`, as the
    // generator plans them (this plans their regions, which costs far more than the cells); none
    // when `cancelled` said so first. Mine shafts and dungeons (underground) are left out: in a
    // debug build their regions over 2048 × 2048 blocks take about 4 s and 3 s to plan (decision
    // E-921).
    [[nodiscard]] static std::optional<QList<Site>> sites(const QcTerrainGenerator &generator, Area area, const Cancelled &cancelled = {});
    // The kinds sites() can find, in a fixed order: "village", "coastal_tower", then
    // the surface structures by name (QcSurfacePlanner::kinds), so a kind keeps its place (and its
    // marker on the map) whatever the world's options turn off.
    [[nodiscard]] static QStringList siteKinds(const QcTerrainGenerator &generator);

    [[nodiscard]] Area area() const { return m_area; }
    [[nodiscard]] int size() const { return m_area.cells(); }
    [[nodiscard]] const Cell &cell(int x, int z) const { return m_cells.at(static_cast<qsizetype>(z) * size() + x); }
    // How many cells of land and river each biome has (index: biome); the sea counts for none.
    [[nodiscard]] QList<int> biomeCells(int biomeCount) const;

private:
    explicit QcBiomeMap(Area area);

    Area m_area;
    QList<Cell> m_cells;
};
