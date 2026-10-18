#pragma once

#include "common/content/definitions/QcMineshaftDefinition.h"
#include "worldgen/mineshafts/QcMineshaftBlocks.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"

#include <QSet>

#include <functional>

// Furnishes the corridors of a laid out mine shaft (decision E-570): spawners in the middle of
// corridors without rails, against a wall off any support (a spawner in the middle lane beside a
// support would close the corridor: the beam leaves no room to jump onto it), thick with cobwebs
// around; rails along other corridors, some with a chest
// minecart; supports (posts and a beam) and, under bridges, their pillars; lights on the beams away
// from every spawn area; cobwebs in the upper corners and a few lower down. Nothing goes into a
// room from a structure.
class QcMineshaftDetails
{
public:
    // A random value in [0, 1) for a purpose and an index.
    using Random = std::function<double(int, int)>;

    // `carts`: whether the content has the cart item (QcSpawnerContent warns otherwise).
    QcMineshaftDetails(const QcMineshaftSettings &settings, const QcMineshaftBlocks &blocks, bool carts);

    void add(QcMineshaftPlan &plan, const Random &random) const;

private:
    // Cells holding a block already, or inside a room.
    struct Taken
    {
        const QcMineshaftPlan &plan;
        QSet<QcBlockPos> cells;

        bool contains(QcBlockPos pos) const;
    };

    // Whether a corridor may take a spawner or rails: on a level, away from the central room and
    // from rooms.
    static bool isPlain(const QcMineshaftPlan &plan, const QcMineshaftPlan::Corridor &corridor);
    void placeSpawners(QcMineshaftPlan &plan, Taken &taken, const Random &random) const;
    void placeRails(QcMineshaftPlan &plan, Taken &taken, const Random &random) const;
    void placeSupports(QcMineshaftPlan &plan, Taken &taken) const;
    // Two posts and a beam at a cell along the corridor, unless a post's cell is taken; the posts'
    // pillars where the corridor bridges a cave.
    void placeSupport(QcMineshaftPlan &plan, Taken &taken, const QcMineshaftPlan::Corridor &corridor, int along) const;
    void placeLights(QcMineshaftPlan &plan, Taken &taken, const Random &random) const;
    void placeCobwebs(QcMineshaftPlan &plan, Taken &taken, const Random &random) const;
    void placeCobwebsIn(QcMineshaftPlan &plan, Taken &taken, int index, const Random &random) const;
    // Dense cobwebs around the spawner in the middle of a corridor.
    void placeSpawnerWebs(QcMineshaftPlan &plan, Taken &taken, const QcMineshaftPlan::Corridor &corridor, const Random &random) const;
    static void place(QcMineshaftPlan &plan, Taken &taken, QcBlockPos at, QcBlockId block);
    // The spawner's cell in a corridor: the middle or the next cell without a support, in the right
    // lane.
    static QcBlockPos spawnerCell(const QcMineshaftPlan::Corridor &corridor);
    // Taxicab distance from a cell to the nearest spawn area.
    static int distanceToSpawners(const QcMineshaftPlan &plan, QcBlockPos pos);

    const QcMineshaftSettings &m_settings;
    const QcMineshaftBlocks &m_blocks;
    bool m_carts = false;
};
