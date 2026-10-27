#pragma once

#include "common/content/definitions/QcMineshaftDefinition.h"
#include "worldgen/mineshafts/QcMineshaftNetwork.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"
#include "worldgen/terrain/QcTerrainColumn.h"

#include <QHash>
#include <QList>

class QcTerrainGenerator;

// What the terrain of one region allows a mine shaft (decision E-570), from the terrain noise alone
// (columns and caves, never generated chunks) and the dungeons planned nearby: a node, corridor or
// room fits where enough ground lies above it and no dungeon is in the way; its floor bridges a
// cave where the noise says it is open, and then every pillar it needs reaches solid ground within
// `maxPillar` blocks. Staircases and the central room need a solid floor. Columns are looked up
// once; one site serves one plan on one thread.
class QcMineshaftSite
{
public:
    using Verdict = QcMineshaftNetwork::Verdict;

    QcMineshaftSite(const QcTerrainGenerator &terrain, const QcMineshaftSettings &settings, int regionX, int regionZ);

    const QcTerrainColumn &column(int x, int z);
    bool cave(QcBlockPos pos);
    Verdict node(const QcMineshaftPlan::Node &node);
    // The central room and rooms from structures: a solid floor everywhere.
    bool roomFits(const QcMineshaftPlan::Node &node, int half, int height);
    Verdict corridor(const QcMineshaftPlan::Corridor &corridor);
    // A box of cells (inclusive corners) clear of every dungeon nearby.
    bool clearOfDungeons(QcBlockPos low, QcBlockPos high) const;
    // No village lies within `margin` blocks of the column, nor a surface structure or the ground
    // blended around it (decision E-889): where an entrance may come up.
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
