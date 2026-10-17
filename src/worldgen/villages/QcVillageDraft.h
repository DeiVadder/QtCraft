#pragma once

#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <QList>
#include <QSet>

#include <array>
#include <optional>
#include <utility>

class QcTerrainGenerator;

// The village of one region while it is planned (decision E-419): the plan under way with the
// footprints of its pieces and roads, its road cells and its three draw sequences. The village
// planner (QcVillagePlanner) passes it to the road network planner (QcRoadNetworkPlanner, QcRoadLayer),
// the piece placer (QcPiecePlacer) and the planners over water (QcBoardwalkPlanner) in turn; every
// draw advances a sequence here, so the plan follows from the order of these calls alone.
struct QcVillageDraft
{
    // Roads are three blocks wide.
    static constexpr int roadHalfWidth = 1;
    // Gap between neighbouring pieces, and between pieces and lamps.
    static constexpr int pieceGap = 2;

    // The eight directions of roads, clockwise from east (x up, z down the screen); even ones are
    // the axes.
    static constexpr std::array<std::pair<int, int>, 8> directions{{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};

    struct Rect
    {
        int x0 = 0;
        int z0 = 0;
        int x1 = 0;
        int z1 = 0;

        bool operator==(const Rect &) const = default;

        bool intersects(Rect other, int gap) const
        {
            return x0 - gap <= other.x1 && x1 + gap >= other.x0 && z0 - gap <= other.z1 && z1 + gap >= other.z0;
        }
    };

    // One straight run of a road (QcVillagePlan::Run) as the pieces and lamps are placed along it.
    struct Road
    {
        int x = 0;
        int z = 0;
        int dx = 0;
        int dz = 0;
        int length = 0;
        // Pieces start this far along the run (past the centre piece or the road it branches off).
        int firstSlot = 0;
        // A side road (QcVillagePlan::branchRoads).
        bool branch = false;
        // Blocks along its road before the run's first cell: lamps keep their spacing over bends.
        int armOffset = 0;
        // The last run of a road that ends in the open: a road end (jetties, stairs to land).
        bool end = false;
        // Sides that take pieces: bit 0 the left (-1), bit 1 the right (1).
        int sides = 3;

        bool diagonal() const { return dx != 0 && dz != 0; }
    };

    // The seeds of the draw sequences: the layout, the variation of the buildings (so the layout
    // does not depend on how many choices and parts the blueprints have) and the crops of the fields.
    struct Seeds
    {
        quint64 layout = 0;
        quint64 variant = 0;
        quint64 crop = 0;
    };

    // The region, its edge and the village's radius in blocks.
    struct Region
    {
        int x = 0;
        int z = 0;
        int spacing = 0;
        int radius = 0;
    };

    QcVillageDraft(const Seeds &draws, Region area) : seeds(draws), region(area) {}

    static qint64 cellKey(int x, int z) { return (qint64(x) << 32) | quint32(z); }
    // The quarter turns clockwise after which the door of a piece (the south side of its first
    // layer) faces (fx, fz), an axis direction.
    static int turnsFacing(int fx, int fz);
    // The brush of a road cell: three across an axis run, three by three on diagonals and bends.
    static Rect brush(std::pair<int, int> cell, int dx, int dz, bool square);

    // The next draw of each sequence, in [0, 1).
    [[nodiscard]] double random();
    [[nodiscard]] double variantRandom();
    [[nodiscard]] double cropRandom();

    // Whether the rectangle lies within the radius around the centre.
    bool insideRadius(Rect rect) const;
    // Whether a road cell lies within `gap` of the rectangle.
    bool touchesRoad(Rect rect, int gap) const;
    // Columns that pieces and lamps keep off (jetties, stairs).
    void markRoad(Rect rect);
    // A road cell at the ground's top block, or at `level` (the road's middle line) where that lies at
    // most one block above or below it (decision E-409); over water a boardwalk deck. Cells under a
    // piece or already a road stay as they are: the first rank laid on a cell keeps it.
    void addRoadCell(const QcTerrainGenerator &terrain, std::pair<int, int> cell, bool branch, std::optional<int> level = std::nullopt);

    QcVillagePlan plan;
    Seeds seeds;
    Region region;
    const QcVillageStyle *style = nullptr;
    // The street plan, the direction its main road leaves the centre in and whether roads wind.
    const QcVillageLayout *shape = nullptr;
    int mainDirection = 0;
    bool winding = false;
    int centreX = 0;
    int centreZ = 0;
    // The positions of the draw sequences.
    int nextRandom = 8;
    int nextVariant = 0;
    int nextCrop = 0;
    QList<Rect> pieces;
    // The extent of every road run, jetty and stairs (the village's area, the wetland's sources).
    QList<Rect> roads;
    // Over water: the stairs down to land (also in `roads`).
    QList<Rect> stairs;
    // Columns of roads, boardwalks, jetties and stairs (cellKey): pieces and lamps keep off them.
    QSet<qint64> roadCells;
    // Pieces placed of each kind of the style.
    QList<int> counts;
    QcBlockId pathStep = qcAirBlockId;
    QcBlockId branchStep = qcAirBlockId;
};
