// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcMineshaftDefinition.h"
#include "worldgen/mineshafts/QcMineshaftBlocks.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"

#include <optional>

class QcMineshaftSite;
class QcTerrainGenerator;

/*! \brief An inclined tunnel from the top level of a mine shaft up to the ground (decision E-895),
    the second kind of surface entrance besides the ladder shaft (QcMineshaftAccess): three cells
    wide and four high like a staircase between levels, it leaves the middle of a free side of the
    central room or of a dead end of the top level one step up and rises one block per cell until
    the ground in front of it lies below its next step (the mouth).

    Steps are full blocks (planks where the ground is open), a support (two posts, a beam in the
    top row) stands every \c {supportSpacing} cells and at the mouth, and with \c {railChance}
    rails rise up the middle lane from the node's edge. Planned from the terrain noise alone like
    the rest of the mine shaft: no cave along it, no village or surface structure where it runs
    near the surface, nothing of the plan but its node within a cell of it, and the whole tunnel
    inside the mine shaft's extent (so inside its region). */
class QcMineshaftIncline
{
public:
    /*! \brief Where a tunnel leaves: a node of the top level (index into the plan's nodes) and
        its side (north, east, south, west). */
    struct Exit
    {
        /*! Index of the node in the plan's nodes. */
        int node = 0;
        /*! The side it leaves through: 0 north, 1 east, 2 south, 3 west. */
        int side = 0;
    };

    QcMineshaftIncline(const QcTerrainGenerator &terrain, const QcMineshaftSettings &settings, const QcMineshaftBlocks &blocks);

    /*! Returns the tunnel out of \a exit of \a plan, with rails when \a rails, if it fits there
        (the ground checked through \a site); std::nullopt otherwise. */
    [[nodiscard]] std::optional<QcMineshaftPlan::Entrance> through(const QcMineshaftPlan &plan, QcMineshaftSite &site, Exit exit, bool rails) const;

private:
    // The tunnel's steps out of the node through its side as a corridor rising one block per cell, as
    // long as it runs in the ground: none when it does not come out within the mine shaft's extent,
    // or comes out at or under the sea or above a drop of more than two blocks.
    [[nodiscard]] std::optional<QcMineshaftPlan::Corridor> layout(const QcMineshaftPlan &plan, QcMineshaftSite &site, Exit exit) const;
    // No cave in the ground along the tunnel, no village or surface structure near it where it runs
    // near the surface or comes out, no dungeon in the way.
    bool clear(QcMineshaftSite &site, const QcMineshaftPlan::Corridor &tunnel) const;
    // One lane of a step with the cells `height` high above it: no cave in the ground there, and
    // where less than `groundCover` lies above, no village or surface structure near.
    bool laneClear(QcMineshaftSite &site, QcBlockPos lane, int height) const;
    // Nothing of the plan but the node `from` within a cell of the tunnel.
    bool apart(const QcMineshaftPlan &plan, const QcMineshaftPlan::Corridor &tunnel, int from) const;
    // The open cells, step linings, supports and rails of the tunnel out of `room`.
    [[nodiscard]] QcMineshaftPlan::Entrance entrance(const QcMineshaftPlan::Node &room, QcMineshaftSite &site, const QcMineshaftPlan::Corridor &tunnel,
                                                     bool rails) const;
    // The ground's top block of the column of `cell`.
    int groundAt(QcMineshaftSite &site, QcBlockPos cell) const;

    const QcTerrainGenerator &m_terrain;
    const QcMineshaftSettings &m_settings;
    const QcMineshaftBlocks &m_blocks;
};
