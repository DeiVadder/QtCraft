// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/dungeons/QcDungeonPlanner.h"
#include "worldgen/generators/QcChunkFurnishing.h"

#include <QHash>
#include <QMutex>

#include <memory>
#include <vector>

/*! \brief Builds the dungeons of the terrain generator into its chunks (decision E-522): each chunk
    writes its own cells of every dungeon whose plan touches it (QcDungeonPlanner, plans cached by
    region), in any order with the same result.

    The room is a shell of wall and moss blocks around air; the corridors' lining replaces only
    what is not rock (cave air, liquids, earth), then the corridors are carved (liquid beside
    them sealed with wall) and the spawner, chests and lights placed. The chests' loot and the
    spawner's kind come with furnishingIn(). Thread-safe. */
class QcDungeonGenerator
{
public:
    QcDungeonGenerator(const QcTerrainGenerator &terrain, const QcContentRegistry &content, quint64 seed);

    /*! Returns the planner whose dungeons this generator builds. */
    const QcDungeonPlanner &planner() const { return m_planner; }
    /*! Returns the plan of the region \a regionX, \a regionZ, planned once (null without a
        dungeon). */
    [[nodiscard]] std::shared_ptr<const QcDungeonPlan> planOf(int regionX, int regionZ) const;
    /*! Returns the dungeons whose cells may lie in the chunk \a pos. */
    QList<std::shared_ptr<const QcDungeonPlan>> plansTouching(QcChunkPos pos) const;
    /*! Writes the cells of the dungeons touching \a chunk into it. */
    void build(QcChunk &chunk) const;
    /*! Returns the spawners (with their creature) and the chests (with the loot table and a
        seed) of the dungeons in the chunk \a pos. */
    QcChunkFurnishing furnishingIn(QcChunkPos pos) const;

private:
    void build(QcChunk &chunk, const QcDungeonPlan &plan) const;
    // Turns liquid of the chunk next to the corridors into wall.
    void sealLiquids(QcChunk &chunk, const QcDungeonPlan &plan) const;
    // A wall block at `pos`: moss with the settings' share (twice that on the floor).
    QcBlockId wallAt(QcBlockPos pos, bool floor) const;

    QcDungeonPlanner m_planner;
    quint64 m_seed = 0;
    QString m_lootTable;
    // Per block state: rock that corridors keep as their wall (solid, opaque, mined with a pickaxe).
    std::vector<bool> m_rock;
    std::vector<bool> m_liquid;
    mutable QMutex m_mutex;
    mutable QHash<qint64, std::shared_ptr<const QcDungeonPlan>> m_plans;
};
