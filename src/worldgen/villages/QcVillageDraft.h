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

    // The style and centre column of the region's village.
    struct Centre
    {
        const QcVillageStyle *style = nullptr;
        int x = 0;
        int z = 0;
    };

    // The street plan of the style chosen by weight, the direction its main road leaves the centre
    // in and whether its roads wind.
    struct Layout
    {
        const QcVillageLayout *shape = nullptr;
        int mainDirection = 0;
        bool winding = false;
    };

    // Over water: the stairs from a boardwalk's end down to land, their extent and the strip
    // beyond them the wetland keeps dry.
    struct Stairs
    {
        QList<QcVillagePlan::Deck> flight;
        Rect extent;
        Rect dry;
    };

    QcVillageDraft(const Seeds &draws, Region area, Centre centre)
        : seeds(draws)
        , region(area)
        , style(centre.style)
        , centreX(centre.x)
        , centreZ(centre.z)
    {
    }

    static qint64 cellKey(int x, int z) { return (static_cast<qint64>(x) << 32) | static_cast<quint32>(z); }
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
    // The level of a road's middle line: its top block, and whether it runs on a bridge there (its
    // top block the deck over a river, decision E-841).
    struct RoadLevel
    {
        int y = 0;
        bool bridge = false;
    };
    // A road cell at the ground's top block, or at `level` (the road's middle line) where that lies at
    // most one block above or below it (decision E-409); over water a boardwalk deck, and where the
    // middle line runs on a bridge a bridge deck over the water. Cells under a piece or already a
    // road stay as they are: the first rank laid on a cell keeps it.
    void addRoadCell(const QcTerrainGenerator &terrain, std::pair<int, int> cell, bool branch, std::optional<RoadLevel> level = std::nullopt);

    // The plan's settings from the style, its road surfaces drawn from the variant sequence before
    // any building's variation.
    void start();
    // Takes the street plan (before the centre piece, which faces the main road).
    void setLayout(Layout layout);
    // The plan names this kind of street plan (a plaza whose ring could not be laid is a cross).
    void setLayoutKind(QcVillageLayout::Kind kind);
    // The arm number of the next road laid.
    [[nodiscard]] int nextArm() const;
    // A laid run of road arm `arm`: its extent, its run in the plan and its length.
    void addRun(const Road &run, int arm);
    // Over water: stairs down to land, kept off by pieces, with their dry strip.
    void addStairs(const Stairs &laid);
    // A jetty's decks; `deck` (its cells) is kept off by pieces and lamps.
    void addJetty(const QList<QcVillagePlan::Deck> &decks, Rect deck);
    // A building over `rect`, with its beds and workstations (`spots`, local), its creatures and its
    // crop cells (`crops`, local); the placed piece, valid until the next piece is added.
    [[nodiscard]] const QcStructurePlacement &addPiece(QcStructurePlacement piece, Rect rect, const QList<QcVillageSpot> &spots,
                                         const QList<QcBlockPos> &crops);
    // On land: doorsteps of a placed piece, as side road where no road lies yet.
    void addThresholds(const QList<QcBlockPos> &steps);
    // The meeting point of the village (its bell).
    void setCentre(QcBlockPos centre) { plan.centre = centre; }
    // One more piece of kind `index` placed.
    void countPiece(int index) { ++counts[index]; }
    // The columns the village covers (pieces, roads and jetties; over water with the wetland's reach).
    void coverArea();

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

private:
    // Over water: the areas the wetland spreads from (pieces and boardwalks, not the stairs), and the
    // village's columns grown by its reach within its region.
    void spreadWetland();
};
