// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/content/worldgen/QcMineshaftDefinition.h"
#include "worldgen/mineshafts/QcMineshaftBlocks.h"
#include "worldgen/mineshafts/QcMineshaftFrame.h"
#include "worldgen/mineshafts/QcMineshaftIncline.h"
#include "worldgen/mineshafts/QcMineshaftNetwork.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"
#include "worldgen/structures/QcStructure.h"

#include <functional>
#include <memory>

class QcMineshaftSite;
class QcTerrainGenerator;

// The ways into a mine shaft besides its corridors (decision E-570): rooms from structures with a
// `"mineshaft"` weight at dead ends (turned so their door faces the corridor), and the surface
// entrance: a ladder shaft from a corner of the central room up through the ground, lined with
// planks where it passes something open, under a headframe of posts and a plank roof, or an
// inclined tunnel (QcMineshaftIncline, decision E-895), the kind drawn by `entranceKinds`. An
// entrance is made with the chance of the biome at the shaft; a ladder shaft where the ground's top
// lies above the sea, the ground around lies level with it (under the headframe within one block,
// around it at most one higher), no cave opens there and no village is around.
class QcMineshaftAccess
{
public:
    using Random = std::function<double(int, int)>;

    QcMineshaftAccess(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcMineshaftSettings &settings,
                      const QcMineshaftBlocks &blocks);

    // Room structures that fit, with their weights.
    qsizetype roomKinds() const { return roomList().size(); }
    // Rooms at the dead ends of the laid out plan, at most `maxRooms`.
    [[nodiscard]] QList<QcMineshaftPlan::Room> roomsOf(const QcMineshaftPlan &plan, const QcMineshaftNetwork &network, QcMineshaftSite &site,
                                                       const Random &random) const;
    // The surface entrance of the central room, where one may come up.
    [[nodiscard]] std::optional<QcMineshaftPlan::Entrance> entranceOf(const QcMineshaftPlan &plan, QcMineshaftSite &site, const QcMineshaftFrame &frame,
                                                                     const Random &random) const;

private:
    struct RoomKind
    {
        std::shared_ptr<const QcStructure> structure;
        int weight = 1;
    };

    // Room structures that fit, with their weights.
    const QList<RoomKind> &roomList() const { return m_rooms; }
    // Whether a pillar of another level would pass through a room at the node or run into its floor.
    static bool crossedByPillars(const QcMineshaftNetwork &network, const QcMineshaftPlan::Node &node);
    // A room at the dead end `at`, turned `turns` quarter turns, if one fits there.
    [[nodiscard]] std::optional<QcStructurePlacement> roomAt(const QcMineshaftPlan::Node &at, int turns, QcMineshaftSite &site, double pick) const;
    // The ladder shaft from the room's north west corner, if it may come up there.
    [[nodiscard]] std::optional<QcMineshaftPlan::Entrance> ladderOf(const QcMineshaftPlan &plan, QcMineshaftSite &site, const QcMineshaftFrame &frame) const;
    // The inclined tunnel through a side of the room or of a dead end of the top level.
    [[nodiscard]] std::optional<QcMineshaftPlan::Entrance> inclineOf(const QcMineshaftPlan &plan, QcMineshaftSite &site, const Random &random) const;
    // The nodes of the top level joined by one corridor only.
    static QList<int> deadEndsOnTop(const QcMineshaftPlan &plan);
    // The shaft's top cell at the column, if an entrance may come up there.
    [[nodiscard]] std::optional<QcBlockPos> surfaceAt(QcMineshaftSite &site, int x, int z, int bottom) const;
    // The ground under the headframe lies within one block of the shaft's top, the ground of the
    // ring around it at most one block higher.
    bool level(QcMineshaftSite &site, QcBlockPos top) const;
    // The headframe over the shaft's top cell `top`.
    [[nodiscard]] QList<QcMineshaftPlan::Placed> headframe(QcBlockPos top) const;

    const QcTerrainGenerator &m_terrain;
    const QcContentRegistry &m_content;
    const QcMineshaftSettings &m_settings;
    const QcMineshaftBlocks &m_blocks;
    QcMineshaftIncline m_incline;
    QList<RoomKind> m_rooms;
    int m_totalWeight = 0;
};
