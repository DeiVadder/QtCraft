#pragma once

#include "worldgen/villages/QcRoadLayer.h"
#include "worldgen/villages/QcVillageDraft.h"

#include <QList>

#include <utility>

// The street plan of a village (decision E-408, E-419): the style's layout by weight with its main
// direction and winding, then its roads (cross, street, T, L, plaza with a ring, tree) from the
// centre or the ring, and the side roads off them, laid by QcRoadLayer. Deterministic and
// thread-safe (const).
class QcRoadNetworkPlanner
{
public:
    using Road = QcVillageDraft::Road;

    QcRoadNetworkPlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content);

    // The street plan, its main direction and whether its roads wind (before the centre piece,
    // which faces the main road).
    void chooseLayout(QcVillageDraft &draft) const;
    // Lays the roads of the street plan around the centre piece; the laid runs in order.
    [[nodiscard]] QList<Road> layRoads(QcVillageDraft &draft) const;

private:
    using Arm = QcRoadLayer::Arm;

    // Lays the arms in order; the laid runs of each go to `roads`, and those of roads that take
    // side roads to `trunks` (one list per road).
    void layArms(QcVillageDraft &draft, const QList<Arm> &arms, QList<Road> &roads, QList<QList<Road>> *trunks) const;
    [[nodiscard]] bool layPlaza(QcVillageDraft &draft, QList<Road> &roads, QList<QList<Road>> &trunks) const;
    // How far a road out from the ring leaves the middle of its side (either way).
    [[nodiscard]] int exitShift(QcVillageDraft &draft, int ring) const;
    // A tree's arms with their bends.
    [[nodiscard]] QList<Arm> treeArms(QcVillageDraft &draft) const;
    // The directions of a tree's arms: the main one first, the others apart from each other.
    [[nodiscard]] QList<int> treeDirections(QcVillageDraft &draft) const;
    void layBranches(QcVillageDraft &draft, const QList<QList<Road>> &trunks, QList<Road> &roads) const;
    // The spots (distance along it, side) where side roads turn off a road.
    [[nodiscard]] QList<std::pair<int, int>> branchSpots(QcVillageDraft &draft, const QList<Road> &trunk) const;
    // A road from the centre in `direction`, its length drawn from the layout's range.
    [[nodiscard]] Arm fromCentre(QcVillageDraft &draft, int direction) const;
    // The smallest distance along `direction` from the centre at which a road clears the centre piece.
    [[nodiscard]] static int clearOfCentre(const QcVillageDraft &draft, int direction);
    [[nodiscard]] static int drawCount(QcVillageDraft &draft, QcVillageRange<int> range);
    [[nodiscard]] static double drawShare(QcVillageDraft &draft, QcVillageRange<double> range);

    QcRoadLayer m_layer;
};
