#include "worldgen/villages/QcPiecePlacer.h"

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcBoardwalkPlanner.h"
#include "worldgen/villages/QcJettyPlanner.h"
#include "worldgen/villages/QcVillageFields.h"
#include "worldgen/villages/QcVillageForecourt.h"

#include <algorithm>
#include <cmath>
#include <tuple>

using namespace Qt::StringLiterals;

namespace {

constexpr auto &directions = QcVillageDraft::directions;
constexpr int roadHalfWidth = QcVillageDraft::roadHalfWidth;
constexpr int pieceGap = QcVillageDraft::pieceGap;
// Random spots tried along the roads for a kind with rules, and road slots tried back from a road
// end for a kind with a water rule.
constexpr int ruleAttempts = 16;
constexpr int waterSlots = 8;
// A piece beside a diagonal road stands at most this far from its middle line.
constexpr int maxSetback = 12;
// Salt of the draw of a style's rare centre (decision E-592).
constexpr quint64 rareCentreSalt = 0x592;

} // namespace

QcPiecePlacer::QcPiecePlacer(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyles &styles)
    : m_terrain(terrain)
    , m_content(content)
    , m_styles(styles)
    , m_settings(content.villageSettings())
{
}

std::optional<int> QcPiecePlacer::floorFor(const QcVillageDraft &draft, Rect rect, int maxSlope) const
{
    if (draft.style->overWater) {
        return QcBoardwalkPlanner::floorFor(m_terrain, draft.style->stilts, {rect.x0, rect.z0}, {rect.x1, rect.z1});
    }

    const std::array<std::pair<int, int>, 5> samples{
        {{rect.x0, rect.z0}, {rect.x1, rect.z0}, {rect.x0, rect.z1}, {rect.x1, rect.z1}, {(rect.x0 + rect.x1) / 2, (rect.z0 + rect.z1) / 2}}};
    int low = QcWorld::worldHeight;
    int high = 0;
    int sum = 0;
    for (const auto &[x, z] : samples) {
        const QcTerrainColumn column = m_terrain.column(x, z);
        if (column.height - 1 < m_terrain.seaLevel() || column.overhang >= 1.0) {
            return std::nullopt;
        }
        low = std::min(low, column.height);
        high = std::max(high, column.height);
        sum += column.height;
    }
    if (high - low > maxSlope) {
        return std::nullopt;
    }
    return int(std::lround(double(sum) / double(samples.size()))) - 1;
}

void QcPiecePlacer::placeCentre(QcVillageDraft &draft) const
{
    // The rare centre (decision E-592) is drawn from its own position, so the other draws of the
    // village stay as they are; where it does not fit the ground the usual centre stands.
    const QcVillageStyle::Piece &rare = draft.style->rareCentre;
    const bool lucky = !rare.blueprints.isEmpty()
                       && qcPositionRandom(qcMixSeed(draft.seeds.layout, rareCentreSalt), draft.region.x, 0, draft.region.z) < rare.chance;
    if (!lucky || !placeCentre(draft, rare.first())) {
        placeCentre(draft, draft.style->centre.first());
    }
}

bool QcPiecePlacer::placeCentre(QcVillageDraft &draft, const QcVillageStyle::Blueprint &centre) const
{
    // The door faces the main road (the axis next to it when that is diagonal).
    const auto [fx, fz] = directions[std::size_t(draft.mainDirection / 2) * 2];
    const int turns = QcVillageDraft::turnsFacing(fx, fz);
    const int sizeX = turns % 2 == 0 ? centre.structure()->sizeX() : centre.structure()->sizeZ();
    const int sizeZ = turns % 2 == 0 ? centre.structure()->sizeZ() : centre.structure()->sizeX();
    const Rect rect{draft.centreX - sizeX / 2, draft.centreZ - sizeZ / 2, draft.centreX - sizeX / 2 + sizeX - 1,
                    draft.centreZ - sizeZ / 2 + sizeZ - 1};
    const std::optional<int> floor = floorFor(draft, rect, m_settings.maxHeightSpread);
    if (!floor) {
        return false;
    }

    // A centre with a cellar (decision E-590) has its ground layer at the floor.
    const QcStructurePlacement &placed = addPiece(draft, centre, rect, {rect.x0, *floor - centre.structure()->groundLayer, rect.z0}, turns);
    draft.setCentre(centre.bell ? placed.toWorld(*centre.bell) : QcBlockPos{draft.centreX, *floor + 1, draft.centreZ});
    return true;
}

void QcPiecePlacer::placeRulePieces(QcVillageDraft &draft, const QList<Road> &roads) const
{
    const QList<QcVillageStyle::Piece> &pieces = draft.style->pieces;
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        const QcVillageStyle::Piece &piece = pieces[i];
        if (!piece.hasRules()) {
            continue;
        }
        // The chance is drawn for every kind with rules, so one kind's rules never shift another's.
        const bool lucky = draft.random() < piece.chance;
        if (!lucky || draft.plan.roadLength < piece.minRoadLength) {
            continue;
        }

        const int wanted = std::max(1, piece.max);
        while (draft.counts[i] < wanted) {
            const bool placed = piece.water > 0 ? placeAtWater(draft, roads, int(i)) : placeAnywhere(draft, roads, int(i));
            if (!placed) {
                break;
            }
            draft.countPiece(int(i));
        }
    }
}

bool QcPiecePlacer::placeAnywhere(QcVillageDraft &draft, const QList<Road> &roads, int index) const
{
    for (int attempt = 0; attempt < ruleAttempts && !roads.isEmpty(); ++attempt) {
        const Road &road = roads[std::min(roads.size() - 1, qsizetype(draft.random() * double(roads.size())))];
        const int side = draft.random() < 0.5 ? -1 : 1;
        const int room = std::max(1, road.length - road.firstSlot - 3);
        const int along = road.firstSlot + 1 + int(draft.random() * room);
        if (placeAt(draft, road, {along, side}, index) > 0) {
            return true;
        }
    }
    return false;
}

bool QcPiecePlacer::placeAtWater(QcVillageDraft &draft, const QList<Road> &roads, int index) const
{
    const QcVillageStyle::Piece &piece = draft.style->pieces[index];
    const qsizetype first = std::min(roads.size() - 1, qsizetype(draft.random() * double(roads.size())));
    for (qsizetype n = 0; n < roads.size(); ++n) {
        const Road &road = roads[(first + n) % roads.size()];
        if (!road.end || road.diagonal()) {
            continue;
        }
        const int endX = road.x + road.dx * (road.length - 1);
        const int endZ = road.z + road.dz * (road.length - 1);
        const QcJettyPlanner::Request request{endX, endZ, road.dx, road.dz, m_terrain.column(endX, endZ).height - 1,
                                              piece.water, piece.jettyWidth, piece.jettyOver, piece.deck, piece.post};
        const std::optional<QList<QcVillagePlan::Deck>> decks = QcJettyPlanner::lay(m_terrain, request);
        if (!decks || decks->isEmpty()) {
            continue;
        }
        const QcBlockPos far = decks->back().at;
        const QcBlockPos near = decks->front().at;
        const Rect jetty{std::min(near.x, far.x) - 2, std::min(near.z, far.z) - 2, std::max(near.x, far.x) + 2, std::max(near.z, far.z) + 2};
        // Over water the wetland spreads from the jetty too, so it keeps the wetland's reach to the
        // region's edge.
        const int keep = draft.style->overWater && draft.style->wetland.bottom != qcAirBlockId ? draft.style->wetland.reach() : 0;
        const int regionX0 = draft.region.x * draft.region.spacing + keep;
        const int regionZ0 = draft.region.z * draft.region.spacing + keep;
        const int room = draft.region.spacing - 2 * keep;
        const bool inRegion = jetty.x0 >= regionX0 && jetty.z0 >= regionZ0 && jetty.x1 < regionX0 + room && jetty.z1 < regionZ0 + room;
        if (!inRegion || std::ranges::any_of(draft.pieces, [&](Rect other) { return other.intersects(jetty, 0); })) {
            continue;
        }

        if (placeBesideEnd(draft, road, index)) {
            draft.addJetty(*decks, {jetty.x0 + 2, jetty.z0 + 2, jetty.x1 - 2, jetty.z1 - 2});
            return true;
        }
    }
    return false;
}

bool QcPiecePlacer::placeBesideEnd(QcVillageDraft &draft, const Road &road, int index) const
{
    // As near to the road's end as the building fits, on either side.
    const QcStructure &structure = *draft.style->pieces[index].first().structure();
    const int depth = std::max(structure.sizeX(), structure.sizeZ());
    for (int slot = 0; slot < waterSlots; ++slot) {
        const int along = road.length - depth - slot;
        for (const int side : {-1, 1}) {
            if (along > road.firstSlot && placeAt(draft, road, {along, side}, index) > 0) {
                return true;
            }
        }
    }
    return false;
}

void QcPiecePlacer::placeAlong(QcVillageDraft &draft, const Road &road) const
{
    for (const int side : {-1, 1}) {
        if ((road.sides & (side < 0 ? 1 : 2)) == 0) {
            continue;
        }
        int along = road.firstSlot + 1;
        while (along < road.length - 2 && draft.plan.pieces.size() <= maxPieces) {
            const int reach = tryPiece(draft, road, along, side);
            if (reach < 0) {
                return;
            }
            along += reach > 0 ? reach + pieceGap : 1;
        }
    }
}

int QcPiecePlacer::tryPiece(QcVillageDraft &draft, const Road &road, int along, int side) const
{
    const int index = choosePiece(draft);
    if (index < 0) {
        return -1;
    }
    const int reach = placeAt(draft, road, {along, side}, index);
    if (reach > 0) {
        draft.countPiece(index);
    }
    return reach;
}

int QcPiecePlacer::placeAt(QcVillageDraft &draft, const Road &road, std::pair<int, int> alongAndSide, int index) const
{
    if (road.diagonal()) {
        return placeAtDiagonal(draft, road, alongAndSide, index);
    }
    const auto [along, side] = alongAndSide;
    // Away from the road on this side; the door faces back to it.
    const int px = -road.dz * side;
    const int pz = road.dx * side;
    const QcVillageStyle::Blueprint &blueprint = chooseBlueprint(draft, draft.style->pieces[index]);
    const QcStructure &structure = *blueprint.structure();
    const int turns = QcVillageDraft::turnsFacing(-px, -pz);
    const int sizeX = turns % 2 == 0 ? structure.sizeX() : structure.sizeZ();
    const int sizeZ = turns % 2 == 0 ? structure.sizeZ() : structure.sizeX();
    const int alongSize = road.dx != 0 ? sizeX : sizeZ;
    const int acrossSize = road.dx != 0 ? sizeZ : sizeX;
    // Over water a piece's floor joins the boardwalk; on land a strip of ground lies between them.
    const int offset = draft.style->overWater ? roadHalfWidth + 1 : pieceOffset;
    const int a1 = along + alongSize - 1;
    const int c1 = offset + acrossSize - 1;
    const int xa = road.x + road.dx * along + px * offset;
    const int za = road.z + road.dz * along + pz * offset;
    const int xb = road.x + road.dx * a1 + px * c1;
    const int zb = road.z + road.dz * a1 + pz * c1;
    const Rect rect{std::min(xa, xb), std::min(za, zb), std::max(xa, xb), std::max(za, zb)};

    const bool hitsPiece = std::ranges::any_of(draft.pieces, [&](Rect other) { return rect.intersects(other, pieceGap); });
    // Over water the whole front of a piece lies on the boardwalk, so its door opens onto it.
    const bool pastEnd = draft.style->overWater && a1 >= road.length;
    if (pastEnd || !draft.insideRadius(rect) || hitsPiece || draft.touchesRoad(rect, offset - roadHalfWidth - 1)) {
        return 0;
    }
    const std::optional<int> floor = floorFor(draft, rect, m_settings.maxPieceSlope);
    if (!floor) {
        return 0;
    }

    std::ignore = addPiece(draft, blueprint, rect, {rect.x0, *floor, rect.z0}, turns);
    return alongSize;
}

int QcPiecePlacer::placeAtDiagonal(QcVillageDraft &draft, const Road &road, std::pair<int, int> alongAndSide, int index) const
{
    const auto [along, side] = alongAndSide;
    const int cx = road.x + road.dx * along;
    const int cz = road.z + road.dz * along;
    // Away from the road on this side is diagonal; the door faces back along one of its axes.
    const int awayX = -road.dz * side;
    const int awayZ = road.dx * side;
    const std::pair<int, int> facing = draft.random() < 0.5 ? std::pair{-awayX, 0} : std::pair{0, -awayZ};
    const QcVillageStyle::Blueprint &blueprint = chooseBlueprint(draft, draft.style->pieces[index]);
    const int turns = QcVillageDraft::turnsFacing(facing.first, facing.second);
    const int sizeX = turns % 2 == 0 ? blueprint.structure()->sizeX() : blueprint.structure()->sizeZ();
    const int sizeZ = turns % 2 == 0 ? blueprint.structure()->sizeZ() : blueprint.structure()->sizeX();

    for (int setback = roadHalfWidth + 2; setback <= maxSetback; ++setback) {
        // The middle of the front row, straight back from the road's middle line.
        const int mx = cx - facing.first * setback;
        const int mz = cz - facing.second * setback;
        const Rect rect = QcVillageForecourt::behindFront({mx, mz}, facing, {sizeX, sizeZ});
        if (draft.touchesRoad(rect, 1)) {
            continue;
        }

        const bool hitsPiece = std::ranges::any_of(draft.pieces, [&](Rect other) { return rect.intersects(other, pieceGap); });
        const std::optional<int> floor = hitsPiece || !draft.insideRadius(rect) ? std::nullopt : floorFor(draft, rect, m_settings.maxPieceSlope);
        // The doors of the blueprint as written (its variations keep them in place).
        const QList<QcBlockPos> steps = floor ? doorsteps(QcStructurePlacement(blueprint.structure(), {rect.x0, *floor, rect.z0}, turns, m_content))
                                              : QList<QcBlockPos>{};
        const QList<std::pair<int, int>> court =
            floor ? QcVillageForecourt::forecourt(m_terrain, draft, rect, facing, steps) : QList<std::pair<int, int>>{};
        if (!floor || court.isEmpty()) {
            return 0;
        }
        std::ignore = addPiece(draft, blueprint, rect, {rect.x0, *floor, rect.z0}, turns);
        for (const std::pair<int, int> &cell : court) {
            draft.addRoadCell(m_terrain, cell, true);
        }
        return std::max(sizeX, sizeZ);
    }
    return 0;
}

int QcPiecePlacer::choosePiece(QcVillageDraft &draft)
{
    const QList<QcVillageStyle::Piece> &pieces = draft.style->pieces;
    const auto open = [&](qsizetype i) { return !pieces[i].hasRules() && (pieces[i].max == 0 || draft.counts[i] < pieces[i].max); };
    int total = 0;
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        total += open(i) ? pieces[i].weight : 0;
    }
    if (total == 0) {
        return -1;
    }

    int pick = int(draft.random() * total);
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        if (!open(i)) {
            continue;
        }
        if (pick < pieces[i].weight) {
            return int(i);
        }
        pick -= pieces[i].weight;
    }
    return -1;
}

void QcPiecePlacer::placeLamps(QcVillageDraft &draft, const Road &road) const
{
    if (draft.style->lamp.blueprints.isEmpty()) {
        return;
    }
    const QcVillageStyle::Blueprint &lamp = draft.style->lamp.first();

    // Every lampSpacing blocks along the whole road, sides taking turns, just beside the road.
    const int spacing = m_settings.lampSpacing;
    for (int d = 0; d < road.length; ++d) {
        const int along = road.armOffset + d;
        if (along % spacing != spacing / 2) {
            continue;
        }
        const int side = (along / spacing) % 2 == 0 ? 1 : -1;
        for (int reach = 1; reach <= roadHalfWidth + 2; ++reach) {
            const int x = road.x + road.dx * d - road.dz * side * reach;
            const int z = road.z + road.dz * d + road.dx * side * reach;
            const Rect rect{x, z, x + lamp.structure()->sizeX() - 1, z + lamp.structure()->sizeZ() - 1};
            if (draft.touchesRoad(rect, 0)) {
                continue;
            }
            const bool free = std::ranges::none_of(draft.pieces, [&](Rect piece) { return rect.intersects(piece, pieceGap); });
            // Inside the radius like the pieces: a wide lamp never crosses the region's edge.
            const std::optional<int> floor = free && draft.insideRadius(rect) ? floorFor(draft, rect, 0) : std::nullopt;
            if (floor) {
                std::ignore = addPiece(draft, lamp, rect, {x, *floor, z}, 0);
            }
            break;
        }
    }
}

const QcVillageStyle::Blueprint &QcPiecePlacer::chooseBlueprint(QcVillageDraft &draft, const QcVillageStyle::Piece &piece)
{
    if (piece.blueprints.size() == 1) {
        return piece.first();
    }

    int total = 0;
    for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
        total += blueprint.weight;
    }
    int pick = int(draft.random() * total);
    for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
        if (pick < blueprint.weight) {
            return blueprint;
        }
        pick -= blueprint.weight;
    }
    return piece.blueprints.back();
}

const QcStructurePlacement &QcPiecePlacer::addPiece(QcVillageDraft &draft, const QcVillageStyle::Blueprint &blueprint, Rect rect, QcBlockPos origin, int turns) const
{
    std::shared_ptr<const QcStructure> structure = blueprint.structure();
    QList<QcVillageSpot> spots = blueprint.spots;
    if (blueprint.variation.varies()) {
        structure = blueprint.variation.build(blueprint.variation.choose([&draft] { return draft.variantRandom(); }));
        spots = m_styles.spotsOf(*structure).spots;
    }
    if (blueprint.fields && !draft.style->crops.isEmpty()) {
        structure = QcVillageFields::plant(*structure, draft.style->crops, m_content, [&draft] { return draft.cropRandom(); });
    }

    const QList<QcBlockPos> crops = blueprint.fields ? QcVillageFields::cropCells(*structure, m_content) : QList<QcBlockPos>();
    const QcStructurePlacement &placed = draft.addPiece(QcStructurePlacement(structure, origin, turns, m_content), rect, spots, crops);
    if (!draft.style->overWater) {
        draft.addThresholds(doorsteps(placed));
    }
    return placed;
}

QList<QcBlockPos> QcPiecePlacer::doorsteps(const QcStructurePlacement &piece) const
{
    // A door's passage runs along its facing; the side of it outside the footprint is the outside.
    const QcStructure &structure = piece.structure();
    QList<QcBlockPos> steps;
    for (int cell = 0; cell < structure.sizeX() * structure.sizeY() * structure.sizeZ(); ++cell) {
        const QcBlockPos local{cell % structure.sizeX(), cell / (structure.sizeX() * structure.sizeZ()), cell / structure.sizeX() % structure.sizeZ()};
        const std::optional<QcBlockId> state = structure.block(local.x, local.y, local.z);
        if (local.y == 0 || !state || !m_content.block(*state).door || m_content.property(*state, u"half"_s) != u"bottom"_s) {
            continue;
        }
        const QString facing = m_content.property(*state, u"facing"_s);
        const int fx = facing == u"east"_s ? 1 : (facing == u"west"_s ? -1 : 0);
        const int fz = facing == u"south"_s ? 1 : (facing == u"north"_s ? -1 : 0);
        for (const int side : {-1, 1}) {
            const QcBlockPos beside = local.offset(fx * side, -1, fz * side);
            if (!structure.contains(beside.x, 0, beside.z)) {
                steps.append(piece.toWorld(beside));
            }
        }
    }
    return steps;
}
