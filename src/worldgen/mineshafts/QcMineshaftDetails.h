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

    // What the details add to a laid out plan (QcMineshaftPlan blocks, spawners, carts, pillars).
    struct Furnishing
    {
        QList<QcMineshaftPlan::Placed> blocks;
        QList<QcMineshaftPlan::Spawner> spawners;
        QList<QcBlockPos> carts;
        QList<QcBlockPos> pillars;
        // Indices of the corridors with rails (QcMineshaftPlan::Corridor::rails).
        QList<int> railed;
    };

    [[nodiscard]] Furnishing furnish(const QcMineshaftPlan &plan, const Random &random) const;

private:
    // The furnishing of a plan under way, with the cells holding a block already.
    struct Work
    {
        const QcMineshaftPlan &plan;
        QSet<QcBlockPos> cells;
        Furnishing furnishing;

        // Whether a cell holds a block already or lies inside a room.
        bool contains(QcBlockPos pos) const;
        void place(QcBlockPos at, QcBlockId block);
        void addSpawner(const QcMineshaftPlan::Spawner &spawner) { furnishing.spawners.append(spawner); }
        void addCart(QcBlockPos at) { furnishing.carts.append(at); }
        void addPillar(QcBlockPos at) { furnishing.pillars.append(at); }
        void addRails(int corridor) { furnishing.railed.append(corridor); }
    };

    // Whether a corridor may take a spawner or rails: on a level, away from the central room and
    // from rooms.
    static bool isPlain(const QcMineshaftPlan &plan, const QcMineshaftPlan::Corridor &corridor);
    void placeSpawners(Work &work, const Random &random) const;
    void placeRails(Work &work, const Random &random) const;
    void placeSupports(Work &work) const;
    // Two posts and a beam at a cell along the corridor, unless a post's cell is taken; the posts'
    // pillars where the corridor bridges a cave.
    void placeSupport(Work &work, const QcMineshaftPlan::Corridor &corridor, int along) const;
    void placeLights(Work &work, const Random &random) const;
    void placeCobwebs(Work &work, const Random &random) const;
    void placeCobwebsIn(Work &work, int index, const Random &random) const;
    // Dense cobwebs around the spawner in the middle of a corridor.
    void placeSpawnerWebs(Work &work, const QcMineshaftPlan::Corridor &corridor, const Random &random) const;
    // The spawner's cell in a corridor: the middle or the next cell without a support, in the right
    // lane.
    static QcBlockPos spawnerCell(const QcMineshaftPlan::Corridor &corridor);
    // Taxicab distance from a cell to the nearest spawn area.
    static int distanceToSpawners(const QList<QcMineshaftPlan::Spawner> &spawners, QcBlockPos pos);

    const QcMineshaftSettings &m_settings;
    const QcMineshaftBlocks &m_blocks;
    bool m_carts = false;
};
