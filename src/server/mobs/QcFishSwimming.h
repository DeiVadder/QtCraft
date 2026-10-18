#pragma once

#include "common/core/QcVec3.h"

#include <QHash>
#include <QList>
#include <QRandomGenerator>

#include <utility>
#include <vector>

class QcBlockAccess;
class QcContentRegistry;
struct QcAquaticDefinition;
struct QcCreature;
struct QcMobDefinition;

// How water creatures move (fish, decision E-502), part of QcMobBehaviour: in water they wander in
// three dimensions without gravity and keep to the water, steered towards their school (cohesion,
// alignment and separation among the nearest creatures of their kind, `schooling`); out of water
// they fall, flop about and count their dry ticks (QcCreature::dryTicks), which QcBodyBurning turns
// into suffocation. The neighbours come from a grid of each kind built once a tick, its cells as
// large as the kind's school radius, so a member looks into 27 cells; the grid keeps its storage
// from tick to tick.
class QcFishSwimming
{
public:
    // Smallest side of a cell of the neighbour grid, in metres (the school radius may be 0).
    static constexpr double minCellSize = 1.0;
    // Chance per tick that a fish on dry ground flops.
    static constexpr double flopChance = 0.1;
    // Seconds between new wandering directions, at least and at most.
    static constexpr double wanderMinSeconds = 1.0;
    static constexpr double wanderMaxSeconds = 4.0;
    // Weight of its own wandering for a fish with a school around it.
    static constexpr double schoolWander = 0.4;

    explicit QcFishSwimming(QRandomGenerator &random);

    // Files the awake water creatures of this tick into the neighbour grid.
    void startTick(const QList<QcCreature> &mobs, const QcContentRegistry &content);
    // One tick of the water creature `mobs[index]`.
    void swim(qsizetype index, QList<QcCreature> &mobs, const QcMobDefinition &definition, const QcBlockAccess &world, double dt);

    // The steering a school adds to one member at `position` moving with `velocity`: towards the
    // centre of the `others` (cohesion), along their mean heading (alignment) and away from those
    // closer than separationDistance (separation), each weighted by the definition. Pure.
    static QcVec3 schooling(const QcVec3 &position, const QList<const QcCreature *> &others, const QcAquaticDefinition &aquatic);
    // Whether the cell at `position` holds water (not lava).
    static bool inWater(const QcBlockAccess &world, const QcVec3 &position);

private:
    // A creature of the grid within the school radius and its squared distance.
    struct Candidate
    {
        double distanceSquared = 0.0;
        const QcCreature *mob = nullptr;
    };
    // A cell of the neighbour grid: the kind and its position in cells of that kind's size.
    using CellKey = std::pair<int, qint64>;

    // The nearest creatures of the same kind within the school radius of `mobs[index]`, at most
    // `neighbours` of them; valid until the next call.
    const QList<const QcCreature *> &neighboursOf(const QList<QcCreature> &mobs, qsizetype index, const QcAquaticDefinition &aquatic);
    // Adds the creatures of the grid cell `cell` that school with `mobs[index]` to m_candidates.
    void collectFrom(const QList<QcCreature> &mobs, qsizetype index, CellKey cell, double radius);
    void swimInWater(QcCreature &mob, const QcMobDefinition &definition, const QcVec3 &school, const QcBlockAccess &world, double dt);
    void flopOnLand(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, double dt);
    // Keeps a wish from leading out of the water: no rising above the surface, turning at shores.
    static QcVec3 keepInWater(const QcVec3 &wish, const QcVec3 &centre, const QcBlockAccess &world);

    static qint64 cellKey(int x, int y, int z);
    static double cellSizeOf(const QcAquaticDefinition &aquatic);
    static CellKey cellOf(int type, const QcVec3 &position, double size);

    QRandomGenerator &m_random;
    // The grid as a counting sort: the creatures of cell c are m_members[m_cellStart[c]] up to
    // m_members[m_cellStart[c + 1]], c from m_cellIndex.
    QHash<CellKey, qsizetype> m_cellIndex;
    std::vector<qsizetype> m_cellOfMob;
    std::vector<qsizetype> m_cellStart;
    std::vector<qsizetype> m_members;
    std::vector<Candidate> m_candidates;
    QList<const QcCreature *> m_found;
};
