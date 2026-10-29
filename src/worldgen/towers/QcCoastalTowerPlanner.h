// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcCoastalTowerDefinition.h"
#include "worldgen/terrain/QcTerrainColumn.h"
#include "worldgen/towers/QcCoastalTowerPlan.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QcTerrainGenerator;

// Plans the coastal tower of a region (decision E-605) from the seed and the terrain noise: whether
// the region has one (`chance`), which structure by weight, and a site on land at the coast. Sites
// are the columns of a grid over the region whose ground lies at a tower's height, tried in a random
// order: the ground under the whole footprint lies at that height without caves and level enough,
// the door opens onto ground at most one block below or above the floor, the open sea lies within
// `coastDistance` blocks, and no village, mine shaft entrance or surface structure
// (QcSurfaceStructures) lies within `clearance` blocks. The tower stays inside its region.
// Thread-safe: plan() only reads immutable state.
class QcCoastalTowerPlanner
{
public:
    using Footprint = QcCoastalTowerPlan::Footprint;

    // Unresolvable structures or blocks are left out with a warning (warnings()); without a
    // structure or the foundation block, towers are off.
    QcCoastalTowerPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);

    const QStringList &warnings() const { return m_warnings; }

    bool enabled() const { return m_enabled; }
    const QcCoastalTowerSettings &settings() const { return m_settings; }
    QcBlockId foundation() const { return m_foundation; }
    // The region's tower before the spacing to the neighbouring regions' (QcCoastalTowerGenerator
    // applies `minDistance`); null without one.
    [[nodiscard]] std::shared_ptr<const QcCoastalTowerPlan> plan(int regionX, int regionZ) const;
    // The region's rank when two towers come closer than `minDistance` (decision E-857): the one of
    // the region with the higher priority stays.
    [[nodiscard]] double priority(int regionX, int regionZ) const;
    // Whether the column belongs to the open sea: land factor below one half, the ground at least
    // `oceanDepth` blocks below the water's surface.
    bool isOcean(int x, int z) const;
    // The ground's top at the column where it lies at a tower's height (from sea level up to
    // `maxRise` above it, no overhang, no cave at the top).
    [[nodiscard]] std::optional<int> coastTop(int x, int z) const;

private:
    struct Kind
    {
        std::shared_ptr<const QcStructure> structure;
        int weight = 1;
    };

    void resolve(const QcContentRegistry &content);
    const Kind &pick(double random) const;
    // The grid of candidate sites: whether each point is land at a tower's height or open sea.
    struct Grid
    {
        int step = 1;
        // Points beyond the sites on each side.
        int reach = 0;
        // The first site's column, and the sites per row and column.
        int firstX = 0;
        int firstZ = 0;
        int count = 0;
        int width = 0;
        std::vector<bool> land;
        std::vector<bool> sea;

        std::size_t index(int i, int j) const { return static_cast<std::size_t>(j) * static_cast<std::size_t>(width) + static_cast<std::size_t>(i); }
        std::pair<int, int> column(int i, int j) const { return {firstX + (i - reach) * step, firstZ + (j - reach) * step}; }
        // A point of the sea within `reach` points.
        bool nearSea(int i, int j) const;
    };

    bool isOceanColumn(const QcTerrainColumn &column) const;
    Grid gridOf(int regionX, int regionZ, int margin) const;
    // The grid columns of the region (`margin` off its edges) where the ground lies at a tower's
    // height with a grid column of the sea near, in a random order.
    QList<std::pair<int, int>> candidates(int regionX, int regionZ, int margin) const;
    // A tower of `kind` centred on the column, if the site fits.
    [[nodiscard]] std::optional<QcCoastalTowerPlan> siteAt(const Kind &kind, int x, int z, double turn) const;
    // The ground's top at a column at a tower's height with no cave in its top layers.
    [[nodiscard]] std::optional<int> groundUnder(int x, int z) const;
    // The floor's height over a footprint: its highest ground top, when every column lies at a
    // tower's height and the columns differ by at most `maxSlope`.
    [[nodiscard]] std::optional<int> floorOver(Footprint footprint) const;
    // The ground's top in front of the door, when one steps from it onto the floor.
    [[nodiscard]] std::optional<QcBlockPos> doorstepOf(const QcStructurePlacement &placement) const;
    // The nearest column of the sea within `coastDistance` of the footprint (rings outwards).
    [[nodiscard]] std::optional<QcBlockPos> oceanNear(Footprint footprint) const;
    [[nodiscard]] std::optional<QcBlockPos> oceanOnRing(Footprint ring) const;
    // No village, no mine shaft entrance and no surface structure within `clearance` of the footprint.
    bool clearOfOthers(Footprint footprint) const;
    bool nearVillage(Footprint around) const;
    bool nearEntrance(Footprint around) const;
    bool nearSurfaceStructure(Footprint around) const;
    double random(int regionX, int regionZ, int purpose, int index = 0) const;

    const QcTerrainGenerator &m_terrain;
    const QcContentRegistry &m_content;
    QcCoastalTowerSettings m_settings;
    QStringList m_warnings;
    quint64 m_seed = 0;
    bool m_enabled = false;
    QcBlockId m_foundation = qcAirBlockId;
    QList<Kind> m_kinds;
    int m_totalWeight = 0;
};
