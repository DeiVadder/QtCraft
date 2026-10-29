// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcPiecePlacer.h"
#include "worldgen/villages/QcRoadNetworkPlanner.h"
#include "worldgen/villages/QcVillageDraft.h"
#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <memory>

class QcTerrainGenerator;

// Lays out the village of one region (decision E-403) from the seed and the terrain noise: a chance
// per region, a centre on flat land of a biome with villages, roads after one of the style's street
// plans (decision E-408: cross, street, T, L, plaza, tree; roads stop at water and steep steps and
// may wind with the terrain) with side roads, pieces of the biome's style on both sides of the
// roads facing them, lamps along stretches without houses. It chooses the region's centre and
// passes the village's draft (QcVillageDraft) in a fixed order to the road network planner
// (QcRoadNetworkPlanner), the piece placer (QcPiecePlacer) and the planners over water
// (QcBoardwalkPlanner, QcJettyPlanner) (decision E-419). Deterministic and thread-safe (const).
class QcVillagePlanner
{
public:
    QcVillagePlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyles &styles, quint64 seed);

    // Edge of a region in blocks, a multiple of the chunk size.
    int spacing() const { return m_spacing; }
    static int spacingOf(const QcVillageSettings &settings);
    // Blocks every village keeps free between its radius and the edge of its region, at least 8:
    // a wetland reaching further would end in a straight line there (QcVillageStyles fits it).
    static int freeMargin(const QcVillageSettings &settings);
    // The village of a region; null when the region has none.
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> plan(int regionX, int regionZ) const;

private:
    // The ground around a centre is sampled this far apart for the flatness and wetness checks.
    static constexpr int flatnessStep = 8;
    // The flatness check leaves out columns this many blocks beyond a river's water (its valley).
    static constexpr int riverValley = 12;

    // The style of the centre column when it is land (or, for a style over water, wet ground too) of a
    // biome with villages.
    const QcVillageStyle *styleAt(int x, int z) const;
    // Whether the columns around a centre, as far as its meeting point may lie from it, belong to
    // biomes with villages, so the bell never stands in a biome without them.
    [[nodiscard]] bool villageBiomeAround(int x, int z) const;
    // Whether the ground around a centre is flat enough (and, over water, carries stilts everywhere).
    bool isFlatAround(const QcVillageStyle &style, int x, int z) const;
    // Over water: QcBoardwalkPlanner::wetAround() around the centre.
    [[nodiscard]] std::optional<int> wetAround(const QcVillageStyle &style, int x, int z) const;
    // The style and centre of the region's village; none when no place in the region fits.
    [[nodiscard]] std::optional<QcVillageDraft::Centre> chooseCentre(int regionX, int regionZ) const;

    const QcTerrainGenerator &m_terrain;
    const QcContentRegistry &m_content;
    const QcVillageStyles &m_styles;
    const QcVillageSettings &m_settings;
    QcVillageDraft::Seeds m_seeds;
    int m_spacing = 0;
    QcRoadNetworkPlanner m_roads;
    QcPiecePlacer m_pieces;
};
