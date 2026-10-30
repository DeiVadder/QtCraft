// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <QList>
#include <QSet>

#include <array>
#include <optional>
#include <utility>

class QcTerrainGenerator;

/*! \brief The village of one region while it is planned (decision E-419): the plan under way with
    the footprints of its pieces and roads, its road cells and its three draw sequences.

    The village planner (QcVillagePlanner) passes it to the road network planner
    (QcRoadNetworkPlanner, QcRoadLayer), the piece placer (QcPiecePlacer) and the planners over
    water (QcBoardwalkPlanner) in turn; every draw advances a sequence here, so the plan follows
    from the order of these calls alone. */
struct QcVillageDraft
{
    /*! Roads are three blocks wide. */
    static constexpr int roadHalfWidth = 1;
    /*! Gap between neighbouring pieces, and between pieces and lamps. */
    static constexpr int pieceGap = 2;

    /*! The eight directions of roads, clockwise from east (x up, z down the screen); even ones
        are the axes. */
    static constexpr std::array<std::pair<int, int>, 8> directions{{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};

    /*! \brief A rectangle of columns from (\c {x0}, \c {z0}) to (\c {x1}, \c {z1}), both
        corners inclusive. \qcfields */
    struct Rect
    {
        int x0 = 0;
        int z0 = 0;
        int x1 = 0;
        int z1 = 0;

        /*! Returns the middle column, summed in 64 bits: near the edge of the world (E-883) two
            corners add up beyond int. */
        int middleX() const { return static_cast<int>((qint64{x0} + x1) / 2); }
        /*! Returns the middle row along z, summed in 64 bits like middleX(). */
        int middleZ() const { return static_cast<int>((qint64{z0} + z1) / 2); }

        bool operator==(const Rect &) const = default;

        /*! Returns whether \a other lies within \a gap columns of this rectangle (0: they
            overlap). */
        bool intersects(Rect other, int gap) const
        {
            return x0 - gap <= other.x1 && x1 + gap >= other.x0 && z0 - gap <= other.z1 && z1 + gap >= other.z0;
        }
    };

    /*! \brief One straight run of a road (QcVillagePlan::Run) as the pieces and lamps are placed
        along it. */
    struct Road
    {
        /*! The x and z of the run's first cell. \qcshared */
        int x = 0;
        int z = 0;
        /*! The run's direction, one of directions. \qcshared */
        int dx = 0;
        int dz = 0;
        /*! The run's cells. */
        int length = 0;
        /*! Pieces start this far along the run (past the centre piece or the road it branches
            off). */
        int firstSlot = 0;
        /*! A side road (QcVillagePlan::branchRoads). */
        bool branch = false;
        /*! Blocks along its road before the run's first cell: lamps keep their spacing over
            bends. */
        int armOffset = 0;
        /*! The last run of a road that ends in the open: a road end (jetties, stairs to land). */
        bool end = false;
        /*! Sides that take pieces: bit 0 the left (-1), bit 1 the right (1). */
        int sides = 3;

        /*! Returns whether the run is diagonal (not along an axis). */
        bool diagonal() const { return dx != 0 && dz != 0; }
    };

    /*! \brief The seeds of the draw sequences: \c {layout} for the layout, \c {variant} for the
        variation of the buildings (so the layout does not depend on how many choices and parts
        the blueprints have) and \c {crop} for the crops of the fields. \qcfields */
    struct Seeds
    {
        quint64 layout = 0;
        quint64 variant = 0;
        quint64 crop = 0;
    };

    /*! \brief The region (\c {x}, \c {z} in regions), its edge (\c {spacing}) and the
        village's \c {radius} in blocks. \qcfields */
    struct Region
    {
        int x = 0;
        int z = 0;
        int spacing = 0;
        int radius = 0;
    };

    /*! \brief The \c {style} and centre column (\c {x}, \c {z}) of the region's village.
        \qcfields */
    struct Centre
    {
        const QcVillageStyle *style = nullptr;
        int x = 0;
        int z = 0;
    };

    /*! \brief The street plan of the style chosen by weight (\c {shape}), the direction its main
        road leaves the centre in (\c {mainDirection}, an index into directions) and whether its
        roads wind (\c {winding}). \qcfields */
    struct Layout
    {
        const QcVillageLayout *shape = nullptr;
        int mainDirection = 0;
        bool winding = false;
    };

    /*! \brief Over water: the stairs from a boardwalk's end down to land (\c {flight}), their
        \c {extent} and the strip beyond them the wetland keeps \c {dry}. \qcfields */
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

    /*! Returns the key of the column \a x, \a z in roadCells. */
    static qint64 cellKey(int x, int z) { return (static_cast<qint64>(x) << 32) | static_cast<quint32>(z); }
    /*! Returns the quarter turns clockwise after which the door of a piece (the south side of
        its first layer) faces (\a fx, \a fz), an axis direction. */
    static int turnsFacing(int fx, int fz);
    /*! Returns the brush of the road cell \a cell of a run in direction (\a dx, \a dz): three
        across an axis run, three by three where \a square (diagonals and bends). */
    static Rect brush(std::pair<int, int> cell, int dx, int dz, bool square);

    /*! Returns the next draw of the layout sequence, in [0, 1). */
    [[nodiscard]] double random();
    /*! Returns the next draw of the variant sequence, in [0, 1). */
    [[nodiscard]] double variantRandom();
    /*! Returns the next draw of the crop sequence, in [0, 1). */
    [[nodiscard]] double cropRandom();

    /*! Returns whether \a rect lies within the square of the radius around the centre. */
    bool insideRadius(Rect rect) const;
    /*! Returns whether \a rect comes within the gap of a piece: \c {pieceGap}, and around the
        centre piece at least its blended ground (decision E-893), so no other piece stands
        on ground that moves. */
    [[nodiscard]] bool nearPiece(Rect rect) const;
    /*! Returns the ground's top block at the column \a x, \a z as the village builds it: inside
        the centre piece its floor, around it the blended ground (decision E-893), elsewhere the
        top block of \a terrain. */
    [[nodiscard]] int groundTop(const QcTerrainGenerator &terrain, int x, int z) const;
    /*! Returns whether a road cell lies within \a gap of \a rect. */
    bool touchesRoad(Rect rect, int gap) const;
    /*! Marks the columns of \a rect as road cells that pieces and lamps keep off (jetties,
        stairs), and adds \a rect to roads. */
    void markRoad(Rect rect);
    /*! \brief The level of a road's middle line: its top block (\c {y}), and whether it runs on
        a bridge there (\c {bridge}; its top block the deck over a river, decision E-841).
        \qcfields */
    struct RoadLevel
    {
        int y = 0;
        bool bridge = false;
    };
    /*! Adds the road cell \a cell (x, z) of a side road when \a branch, else of a road from the
        centre: on land at the ground's top block of \a terrain, or at \a level (the road's
        middle line) where that lies at most one block above or below it (decision E-409); in a
        style over water a boardwalk deck; on land over water where the middle line runs on a
        bridge the style's bridge deck, and a water cell without a bridge stays water (decision
        E-907).
        Cells under a piece or already a road stay as they are: the first rank laid on a cell
        keeps it. */
    void addRoadCell(const QcTerrainGenerator &terrain, std::pair<int, int> cell, bool branch, std::optional<RoadLevel> level = std::nullopt);

    /*! Starts the plan: its id and settings from the style, its road surfaces drawn from the
        variant sequence before any building's variation, the piece counts at 0. */
    void start();
    /*! Takes the street plan \a layout (before the centre piece, which faces the main road). */
    void setLayout(Layout layout);
    /*! Names \a kind as the plan's kind of street plan (a plaza whose ring could not be laid is
        a cross). */
    void setLayoutKind(QcVillageLayout::Kind kind);
    /*! Returns the arm number of the next road laid. */
    [[nodiscard]] int nextArm() const;
    /*! Adds the laid \a run of road arm \a arm: its extent, its run in the plan and its
        length. */
    void addRun(const Road &run, int arm);
    /*! Over water: adds the stairs \a laid down to land, kept off by pieces, with their dry
        strip. */
    void addStairs(const Stairs &laid);
    /*! Adds a jetty's \a decks; \a deck (its cells) is kept off by pieces and lamps. */
    void addJetty(const QList<QcVillagePlan::Deck> &decks, Rect deck);
    /*! Adds the building \a piece over \a rect, with its beds and workstations (\a spots,
        local), its creatures and its crop cells (\a crops, local); returns the placed piece,
        valid until the next piece is added. */
    [[nodiscard]] const QcStructurePlacement &addPiece(QcStructurePlacement piece, Rect rect, const QList<QcVillageSpot> &spots,
                                         const QList<QcBlockPos> &crops);
    /*! On land: adds the doorsteps \a steps of a placed piece, as side road where no road lies
        yet. */
    void addThresholds(const QList<QcBlockPos> &steps);
    /*! Sets the meeting point of the village (its bell) to \a centre. */
    void setCentre(QcBlockPos centre) { plan.centre = centre; }
    /*! Counts one more piece of kind \a index placed. */
    void countPiece(int index) { ++counts[index]; }
    /*! Sets the columns the village covers (pieces, roads and jetties, the centre's blended
        ground; over water with the wetland's reach, within the region). */
    void coverArea();

    /*! The plan under way. */
    QcVillagePlan plan;
    /*! The seeds of the draw sequences. */
    Seeds seeds;
    /*! The region and the village's radius. */
    Region region;
    /*! The village's style. */
    const QcVillageStyle *style = nullptr;
    /*! The street plan (setLayout()). */
    const QcVillageLayout *shape = nullptr;
    /*! The direction the main road leaves the centre in, an index into directions. */
    int mainDirection = 0;
    /*! Whether the roads wind. */
    bool winding = false;
    /*! The x and z of the centre column. \qcshared */
    int centreX = 0;
    int centreZ = 0;
    /*! The next index of the layout, variant and crop draw sequence. \qcshared */
    int nextRandom = 8;
    int nextVariant = 0;
    int nextCrop = 0;
    /*! The footprints of the placed pieces, the centre piece first. */
    QList<Rect> pieces;
    /*! The extent of every road run, jetty and stairs (the village's area, the wetland's
        sources). */
    QList<Rect> roads;
    /*! Over water: the stairs down to land (also in \c {roads}). */
    QList<Rect> stairs;
    /*! Columns of roads, boardwalks, jetties and stairs (cellKey): pieces and lamps keep off
        them. */
    QSet<qint64> roadCells;
    /*! Pieces placed of each kind of the style. */
    QList<int> counts;
    /*! The step blocks of the road surfaces drawn for the roads from the centre and the side
        roads (start()); air: no steps. \qcshared */
    QcBlockId pathStep = qcAirBlockId;
    QcBlockId branchStep = qcAirBlockId;

private:
    // Over water: the areas the wetland spreads from (pieces and boardwalks, not the stairs), and the
    // village's columns grown by its reach within its region.
    void spreadWetland();
};
