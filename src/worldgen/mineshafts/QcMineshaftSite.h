// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcMineshaftDefinition.h"
#include "worldgen/mineshafts/QcMineshaftNetwork.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"
#include "worldgen/terrain/QcTerrainColumn.h"

#include <QHash>
#include <QList>

class QcTerrainGenerator;

/*! \brief What the terrain of one region allows a mine shaft (decision E-570), from the terrain
    noise alone (columns and caves, never generated chunks) and the dungeons planned nearby: a node,
    corridor or room fits where enough ground lies above it and no dungeon is in the way; its floor
    bridges a cave where the noise says it is open, and then every pillar it needs reaches solid
    ground within \c {maxPillar} blocks.

    Staircases and the central room need a solid floor. Columns are looked up once; one site
    serves one plan on one thread. */
class QcMineshaftSite
{
public:
    /*! Whether an element may be made, and whether it bridges a cave. */
    using Verdict = QcMineshaftNetwork::Verdict;

    QcMineshaftSite(const QcTerrainGenerator &terrain, const QcMineshaftSettings &settings, int regionX, int regionZ);

    /*! Returns the terrain column at \a x, \a z, looked up once and then cached. */
    const QcTerrainColumn &column(int x, int z);
    /*! Returns whether the terrain noise carves a cave at \a pos. */
    bool cave(QcBlockPos pos);
    /*! Returns whether \a node fits (enough ground above its middle and corners, clear of
        dungeons) and whether its floor bridges a cave; a bridge fits only where the pillar under
        its middle reaches the ground (or the middle stands on ground). */
    Verdict node(const QcMineshaftPlan::Node &node);
    /*! Returns whether a room \a half cells on each side of the middle of \a node and \a height
        cells high fits: enough ground above its middle and corners, clear of dungeons, and a
        solid floor everywhere (the central room and rooms from structures). */
    bool roomFits(const QcMineshaftPlan::Node &node, int half, int height);
    /*! Returns whether \a corridor fits (clear of dungeons, enough ground above every lane) and
        whether its floor bridges a cave; a staircase never bridges, and a bridge fits only where
        the pillars of its supports reach the ground. */
    Verdict corridor(const QcMineshaftPlan::Corridor &corridor);
    /*! Returns whether the box of cells \a low to \a high (inclusive corners), grown by one cell,
        is clear of every dungeon nearby. */
    bool clearOfDungeons(QcBlockPos low, QcBlockPos high) const;
    /*! Returns whether no village lies within \a margin blocks of the column \a x, \a z, nor a
        surface structure or the ground blended around it (decision E-889): where an entrance may
        come up. */
    bool awayFromSettlements(int x, int z, int margin) const;

private:
    // At least `groundCover` blocks of ground above the open cells up to `top` (exclusive) at a column.
    bool covered(int x, int z, int top);
    // A pillar down from below the floor cell `floor` reaches solid ground in time.
    bool pillarReachesGround(QcBlockPos floor);

    struct Box
    {
        QcBlockPos low;
        QcBlockPos high;
    };

    const QcTerrainGenerator &m_terrain;
    const QcMineshaftSettings &m_settings;
    // NOLINT(qc-member-access): the cache of column(), which is its only user (lookup and fill).
    QHash<qint64, QcTerrainColumn> m_columns;
    QList<Box> m_dungeons;
};
