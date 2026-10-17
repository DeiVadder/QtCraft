#pragma once

#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <QList>
#include <QSet>

#include <array>
#include <functional>
#include <memory>

class QcTerrainGenerator;

// Lays out the village of one region (decision E-403) from the seed and the terrain noise: a chance
// per region, a centre on flat land of a biome with villages, roads after one of the style's street
// plans (decision E-408: cross, street, T, L, plaza, tree; roads stop at water and steep steps and
// may wind with the terrain) with side roads, pieces of the biome's style on both sides of the
// roads facing them, lamps along stretches without houses. Deterministic and thread-safe (const).
class QcVillagePlanner
{
public:
    // Pieces of one village at most (the centre and lamps not counted).
    static constexpr int maxPieces = 28;

    QcVillagePlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyles &styles, quint64 seed);

    // Edge of a region in blocks, a multiple of the chunk size.
    int spacing() const { return m_spacing; }
    static int spacingOf(const QcVillageSettings &settings);
    // Blocks every village keeps free between its radius and the edge of its region, at least 8:
    // a wetland reaching further would end in a straight line there (QcVillageStyles fits it).
    static int freeMargin(const QcVillageSettings &settings);
    // The village of a region; null when the region has none.
    [[nodiscard]] std::shared_ptr<const QcVillagePlan> plan(int regionX, int regionZ) const;

private:
    // The ground around a centre is sampled this far apart for the flatness and wetness checks.
    static constexpr int flatnessStep = 8;
    // Over water, at least this share of the ground around the centre carries stilts.
    static constexpr double stiltShare = 0.6;
    // Roads are three blocks wide; on land pieces start one block of grass beyond their edge.
    static constexpr int roadHalfWidth = 1;
    static constexpr int pieceOffset = roadHalfWidth + 2;
    // Gap between neighbouring pieces, and between pieces and lamps.
    static constexpr int pieceGap = 2;
    // A road ends where its ground steps up or down by more than this.
    static constexpr int roadStep = 2;

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

    // A road to lay (decision E-408): from (x, z) `length` cells in `direction` (an index into
    // `directions`), turning by `bends` (distance along it, eighths of a turn; positive clockwise),
    // winding with the terrain, cut short at water, steep steps, pieces and the village's radius.
    struct Arm
    {
        int x = 0;
        int z = 0;
        int direction = 0;
        int length = 0;
        int firstSlot = 0;
        bool branch = false;
        QList<std::pair<int, int>> bends;
        bool winding = false;
        // Laid only in full, without a road end (a ring back to its first cell, a plaza's spoke).
        bool closed = false;
        int sides = 3;
    };

    // The eight directions of roads, clockwise from east (x up, z down the screen); even ones are
    // the axes.
    static constexpr std::array<std::pair<int, int>, 8> directions{{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};

    // The state of one layout under way.
    struct Layout
    {
        QcVillagePlan plan;
        const QcVillageStyle *style = nullptr;
        // The street plan, the direction its main road leaves the centre in and whether roads wind.
        const QcVillageLayout *shape = nullptr;
        int mainDirection = 0;
        bool winding = false;
        int regionX = 0;
        int regionZ = 0;
        int centreX = 0;
        int centreZ = 0;
        int nextRandom = 8;
        // The variation of the buildings draws from its own sequence, so the layout does not
        // depend on how many choices and parts the blueprints have.
        int nextVariant = 0;
        // The crops of the fields draw from a third sequence.
        int nextCrop = 0;
        QList<Rect> pieces;
        // The extent of every road run, jetty and stairs (the village's area, the wetland's sources).
        QList<Rect> roads;
        // Over water: the stairs down to land (also in `roads`).
        QList<Rect> stairs;
        // Columns of roads, boardwalks, jetties and stairs (cellKey): pieces and lamps keep off them.
        QSet<qint64> roadCells;
        QList<int> counts;
        QcBlockId pathStep = qcAirBlockId;
        QcBlockId branchStep = qcAirBlockId;
    };

    static qint64 cellKey(int x, int z) { return (qint64(x) << 32) | quint32(z); }
    // The quarter turns clockwise after which the door of a piece (the south side of its first
    // layer) faces (fx, fz), an axis direction.
    static int turnsFacing(int fx, int fz);

    [[nodiscard]] double random(Layout &layout) const;
    [[nodiscard]] double variantRandom(Layout &layout) const;
    [[nodiscard]] double cropRandom(Layout &layout) const;
    // The road surfaces of both ranks, drawn from the variant seed before any building's variation.
    void chooseRoads(Layout &layout) const;
    // The style of the centre column when it is land (or, for a style over water, wet ground too) of a
    // biome with villages.
    const QcVillageStyle *styleAt(int x, int z) const;
    // Whether the ground around a centre is flat enough (and, over water, carries stilts everywhere).
    bool isFlatAround(const QcVillageStyle &style, int x, int z) const;
    // Over water: the samples of the ground around a centre under water, when most of them carry
    // stilts and enough of them are water; std::nullopt otherwise.
    [[nodiscard]] std::optional<int> wetAround(const QcVillageStyle &style, int x, int z) const;
    // The style and centre of the region's village, if any (Layout::style null: none).
    void chooseCentre(Layout &layout) const;
    // Floor height for a piece over `rect` (the ground's top block), or std::nullopt where the
    // ground is too steep or wet; over water the floor height of the stilts (decision E-407), or
    // std::nullopt where the ground cannot carry them.
    [[nodiscard]] std::optional<int> floorFor(const Layout &layout, Rect rect, int maxSlope) const;
    // The centre piece, its door towards the main road.
    void placeCentre(Layout &layout) const;

    // Street plans (QcVillagePlannerLayouts.cpp): the style's layout by weight, its main direction
    // and winding, then its roads from the centre (or the ring) and the side roads off them.
    void chooseLayout(Layout &layout) const;
    void layRoads(Layout &layout, QList<Road> &roads) const;
    // Lays the arms in order; the laid runs of each go to `roads`, and those of roads that take
    // side roads to `trunks` (one list per road).
    void layArms(Layout &layout, const QList<Arm> &arms, QList<Road> &roads, QList<QList<Road>> *trunks) const;
    [[nodiscard]] bool layPlaza(Layout &layout, QList<Road> &roads, QList<QList<Road>> &trunks) const;
    // How far a road out from the ring leaves the middle of its side (either way).
    [[nodiscard]] int exitShift(Layout &layout, int ring) const;
    // A tree's arms with their bends.
    [[nodiscard]] QList<Arm> treeArms(Layout &layout) const;
    // The directions of a tree's arms: the main one first, the others apart from each other.
    [[nodiscard]] QList<int> treeDirections(Layout &layout) const;
    void layBranches(Layout &layout, const QList<QList<Road>> &trunks, QList<Road> &roads) const;
    // The spots (distance along it, side) where side roads turn off a road.
    [[nodiscard]] QList<std::pair<int, int>> branchSpots(Layout &layout, const QList<Road> &trunk) const;
    // A road from the centre in `direction`, its length drawn from the layout's range.
    [[nodiscard]] Arm fromCentre(Layout &layout, int direction) const;
    [[nodiscard]] int drawCount(Layout &layout, QcVillageRange<int> range) const;
    [[nodiscard]] double drawShare(Layout &layout, QcVillageRange<double> range) const;

    // Road laying (QcVillagePlannerRoads.cpp).
    // Lays an arm: its runs, cut short at water, steep steps, pieces and the radius, recorded with
    // their cells; empty when nothing past its first slot is left (or a closed arm is cut).
    QList<Road> layArm(Layout &layout, const Arm &arm) const;
    // The runs layArm() would lay, without recording them.
    QList<Road> walkArm(Layout &layout, const Arm &arm) const;
    // The ground height of the next cell of a road, coming from ground of height `previous`;
    // std::nullopt where the road ends there (water, a steep step, a piece, the radius).
    [[nodiscard]] std::optional<int> roadGround(const Layout &layout, std::pair<int, int> cell, int direction, int previous,
                                                bool checkPieces) const;
    // The turn (-1, 0, 1 eighths) of a winding road towards the flatter neighbouring direction, at
    // most one eighth off its `heading`.
    int windTurn(Layout &layout, std::pair<int, int> cell, int direction, int heading) const;
    // The cells of laid runs outside the pieces: ground cells on land, boardwalk decks over water;
    // the first rank laid on a cell keeps it.
    void recordRuns(Layout &layout, const QList<Road> &runs, bool closed) const;
    // The cells of one run; `joins` (previous, next) squares its first or last cell's brush.
    void paintRun(Layout &layout, const Road &run, std::pair<bool, bool> joins) const;
    // A road cell at the ground's top block, or at `level` (the road's middle line) where that lies at
    // most one block above or below it (decision E-409).
    void addRoadCell(Layout &layout, std::pair<int, int> cell, bool branch, std::optional<int> level = std::nullopt) const;
    // The brush of a road cell: three across an axis run, three by three on diagonals and bends.
    static Rect brush(std::pair<int, int> cell, int dx, int dz, bool square);
    // Columns that pieces and lamps keep off (jetties, stairs).
    void markRoad(Layout &layout, Rect rect) const;
    // Whether a road cell lies within `gap` of the rectangle.
    bool touchesRoad(const Layout &layout, Rect rect, int gap) const;
    // The smallest distance along `direction` from the centre at which a road clears the centre piece.
    int clearOfCentre(const Layout &layout, int direction) const;
    // Over water: stairs from the end of a laid boardwalk down to dry land, where they fit, with the
    // land access kept dry by the wetland.
    void layStairs(Layout &layout, const Road &road) const;
    // Over water: the areas the wetland spreads from, and the village's columns grown by its reach.
    void planWetland(Layout &layout) const;
    // The kinds with rules (decision E-404, E-408) whose rules hold, each up to its maximum (at least one).
    void placeRulePieces(Layout &layout, const QList<Road> &roads) const;
    // A kind without a water rule on a random free spot along a road; false when none fits.
    bool placeAnywhere(Layout &layout, const QList<Road> &roads, int index) const;
    // A kind with a water rule beside the end of a road with water ahead, and its jetty from the
    // road end out over the water; false when no road end fits.
    bool placeAtWater(Layout &layout, const QList<Road> &roads, int index) const;
    // A building of kind `index` beside the last blocks of the road; false when none fits.
    bool placeBesideEnd(Layout &layout, const Road &road, int index) const;
    void placeAlong(Layout &layout, const Road &road) const;
    // Places a piece `along` the road on `side` (-1 left, 1 right); returns how far along it reaches,
    // 0 when it did not fit and -1 when no piece is left to place.
    int tryPiece(Layout &layout, const Road &road, int along, int side) const;
    // Places a building of kind `index` like tryPiece(); returns how far along it reaches, 0 when it
    // did not fit.
    int placeAt(Layout &layout, const Road &road, std::pair<int, int> alongAndSide, int index) const;
    // placeAt() beside a diagonal run (QcVillagePlannerRoads.cpp): the door faces the road along the
    // nearer axis, and a forecourt of side road leads from the door to the road.
    int placeAtDiagonal(Layout &layout, const Road &road, std::pair<int, int> alongAndSide, int index) const;
    // The footprint of a piece of `size` whose front row has its middle at `middle`, the door facing
    // `facing` (an axis direction).
    static Rect behindFront(std::pair<int, int> middle, std::pair<int, int> facing, std::pair<int, int> size);
    // The forecourt cells from the front of a piece (door facing `facing`) to the road, three blocks
    // wide in front of each of its `doorsteps` (else of the middle of the front); empty when one of
    // them does not reach the road or the forecourt crosses a piece.
    QList<std::pair<int, int>> forecourt(const Layout &layout, Rect rect, std::pair<int, int> facing, const QList<QcBlockPos> &doorsteps) const;
    // One column of a forecourt, cellAt(1), cellAt(2), ... up to the first road cell; empty when it
    // does not reach a road within the forecourt's reach, std::nullopt when it crosses a piece or water.
    [[nodiscard]] std::optional<QList<std::pair<int, int>>> forecourtColumn(const Layout &layout,
                                                                          const std::function<std::pair<int, int>(int)> &cellAt) const;
    // A kind without rules chosen by weight among those not at their maximum; -1 when none is left.
    int choosePiece(Layout &layout) const;
    // A blueprint of the kind chosen by weight.
    const QcVillageStyle::Blueprint &chooseBlueprint(Layout &layout, const QcVillageStyle::Piece &piece) const;
    void placeLamps(Layout &layout, const Road &road) const;
    // Adds a building of the blueprint with its choices and parts drawn, its beds, workstations and
    // creatures.
    void addPiece(Layout &layout, const QcVillageStyle::Blueprint &blueprint, Rect rect, QcBlockPos origin, int turns) const;
    // On land: the doorsteps of a placed piece's outer doors (QcVillagePlan::thresholds).
    void addThresholds(Layout &layout, const QcStructurePlacement &piece) const;
    // The cells in front of the outer doors of a placed piece, at the height of its floor.
    QList<QcBlockPos> doorsteps(const QcStructurePlacement &piece) const;
    bool insideRadius(const Layout &layout, Rect rect) const;

    const QcTerrainGenerator &m_terrain;
    const QcContentRegistry &m_content;
    const QcVillageStyles &m_styles;
    const QcVillageSettings &m_settings;
    quint64 m_seed = 0;
    quint64 m_variantSeed = 0;
    quint64 m_cropSeed = 0;
    int m_spacing = 0;
};
