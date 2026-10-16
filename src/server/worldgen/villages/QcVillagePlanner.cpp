#include "QcVillagePlanner.h"

#include "QcBoardwalkPlanner.h"
#include "QcNoise.h"
#include "QcTerrainGenerator.h"
#include "QcJettyPlanner.h"
#include "QcVillageFields.h"
#include "QcVillageRoads.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Pieces keep this many blocks to the region's edge besides the radius.
constexpr int regionMargin = 8;
// Random spots tried along the roads for a kind with rules, and road slots tried back from a road
// end for a kind with a water rule.
constexpr int ruleAttempts = 16;
constexpr int waterSlots = 8;

// The lists of a finished plan without the room they grew into: the generator caches hundreds of
// plans (QcVillageGenerator::cachedPlans).
void squeeze(QcVillagePlan &plan)
{
    plan.pieces.squeeze();
    plan.roads.squeeze();
    plan.branchRoads.squeeze();
    plan.thresholds.squeeze();
    plan.runs.squeeze();
    plan.steps.squeeze();
    plan.decks.squeeze();
    plan.wetland.wet.squeeze();
    plan.wetland.dry.squeeze();
    plan.beds.squeeze();
    plan.workstations.squeeze();
    plan.mobs.squeeze();
}

} // namespace

int QcVillagePlanner::turnsFacing(int fx, int fz)
{
    if (fz > 0) {
        return 0;
    }
    if (fx < 0) {
        return 1;
    }
    return fz < 0 ? 2 : 3;
}

QcVillagePlanner::QcVillagePlanner(const QcTerrainGenerator &terrain, const QcContentRegistry &content, const QcVillageStyles &styles,
                                   quint64 seed)
    : m_terrain(terrain)
    , m_content(content)
    , m_styles(styles)
    , m_settings(content.villageSettings())
    , m_seed(qcMixSeed(seed, qcNameSalt(u"qtcraft:villages"_s)))
    , m_variantSeed(qcMixSeed(m_seed, qcNameSalt(u"qtcraft:village_variants"_s)))
    , m_cropSeed(qcMixSeed(m_seed, qcNameSalt(u"qtcraft:village_crops"_s)))
    , m_spacing(spacingOf(m_settings))
{
}

int QcVillagePlanner::spacingOf(const QcVillageSettings &settings)
{
    // Regions are whole chunks, so a chunk lies in exactly one region.
    return std::max(QcWorld::chunkSize * 8, settings.spacing / QcWorld::chunkSize * QcWorld::chunkSize);
}

int QcVillagePlanner::freeMargin(const QcVillageSettings &settings)
{
    // Centres keep this much to the region's edges (chooseCentre), the village the radius to its centre.
    return std::max(settings.radius + regionMargin, spacingOf(settings) / 4) - settings.radius;
}

double QcVillagePlanner::random(Layout &layout) const
{
    return qcPositionRandom(m_seed, layout.regionX, layout.nextRandom++, layout.regionZ);
}

double QcVillagePlanner::variantRandom(Layout &layout) const
{
    return qcPositionRandom(m_variantSeed, layout.regionX, layout.nextVariant++, layout.regionZ);
}

double QcVillagePlanner::cropRandom(Layout &layout) const
{
    return qcPositionRandom(m_cropSeed, layout.regionX, layout.nextCrop++, layout.regionZ);
}

void QcVillagePlanner::chooseRoads(Layout &layout) const
{
    // Indices below the variant sequence's (which starts at 0).
    const QcVillageStyle::Road &road =
        QcVillageRoads::choose(layout.style->roads, qcPositionRandom(m_variantSeed, layout.regionX, -1, layout.regionZ));
    const QcVillageStyle::Road &branch =
        QcVillageRoads::choose(layout.style->branchRoads, qcPositionRandom(m_variantSeed, layout.regionX, -2, layout.regionZ));
    layout.plan.pathBlock = road.block;
    layout.plan.branchPathBlock = branch.block;
    layout.pathStep = road.step;
    layout.branchStep = branch.step;
}

const QcVillageStyle *QcVillagePlanner::styleAt(int x, int z) const
{
    const QcTerrainColumn column = m_terrain.column(x, z);
    if (column.landFactor < 0.99 || column.overhang >= 1.0) {
        return nullptr;
    }
    const QcBiomeDefinition *biome = m_content.biome(m_terrain.biomeName(column.biome));
    const QcVillageStyle *style = biome ? m_styles.forBiome(*biome) : nullptr;
    // A style over water stands on wet ground too, one on land only above the sea level.
    if (!style || (!style->overWater && column.height - 1 <= m_terrain.seaLevel())) {
        return nullptr;
    }
    return style;
}

bool QcVillagePlanner::isFlatAround(const QcVillageStyle &style, int x, int z) const
{
    const int reach = m_settings.radius / 2 / flatnessStep * flatnessStep;
    if (style.overWater) {
        return wetAround(style, x, z).has_value();
    }
    int low = QcWorld::worldHeight;
    int high = 0;
    for (int dz = -reach; dz <= reach; dz += flatnessStep) {
        for (int dx = -reach; dx <= reach; dx += flatnessStep) {
            const int height = m_terrain.column(x + dx, z + dz).height;
            low = std::min(low, height);
            high = std::max(high, height);
        }
    }
    return high - low <= m_settings.maxHeightSpread;
}

void QcVillagePlanner::chooseCentre(Layout &layout) const
{
    // The first of a few places in the region that is flat land of a biome with villages. Centres
    // keep a quarter of the region to its edges, so neighbouring villages lie half a region apart.
    // Once a place over water fits, the later places are tried too and the wettest one wins: a
    // village over water stands where its biome has the most water.
    const int margin = m_settings.radius + freeMargin(m_settings);
    const int room = std::max(1, m_spacing - 2 * margin);
    const int regionX = layout.regionX;
    const int regionZ = layout.regionZ;
    int wettest = -1;
    for (int attempt = 0; attempt < m_settings.attempts && (!layout.style || layout.style->overWater); ++attempt) {
        const int x = regionX * m_spacing + margin + int(qcPositionRandom(m_seed, regionX, -1 - 2 * attempt, regionZ) * room);
        const int z = regionZ * m_spacing + margin + int(qcPositionRandom(m_seed, regionX, -2 - 2 * attempt, regionZ) * room);
        const QcVillageStyle *style = styleAt(x, z);
        if (!style || (layout.style && !style->overWater)) {
            continue;
        }
        const std::optional<int> wet = style->overWater ? wetAround(*style, x, z) : std::nullopt;
        const bool fits = style->overWater ? wet && *wet > wettest : isFlatAround(*style, x, z);
        if (fits) {
            layout.style = style;
            layout.centreX = x;
            layout.centreZ = z;
            wettest = wet.value_or(-1);
        }
    }
}

std::optional<int> QcVillagePlanner::floorFor(const Layout &layout, Rect rect, int maxSlope) const
{
    if (layout.style->overWater) {
        return QcBoardwalkPlanner::floorFor(m_terrain, layout.style->stilts, {rect.x0, rect.z0}, {rect.x1, rect.z1});
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

std::shared_ptr<const QcVillagePlan> QcVillagePlanner::plan(int regionX, int regionZ) const
{
    Layout layout;
    layout.regionX = regionX;
    layout.regionZ = regionZ;
    if (qcPositionRandom(m_seed, regionX, 0, regionZ) >= m_settings.chance || m_styles.isEmpty()) {
        return nullptr;
    }

    chooseCentre(layout);
    if (!layout.style) {
        return nullptr;
    }

    layout.plan.id = (qint64(regionX) << 32) | quint32(regionZ);
    layout.plan.style = layout.style->name;
    chooseRoads(layout);
    layout.plan.foundation = layout.style->foundation;
    if (layout.style->overWater) {
        layout.plan.stiltPost = layout.style->stilts.post;
        layout.plan.stiltSpacing = layout.style->stilts.spacing;
    }
    layout.counts.fill(0, layout.style->pieces.size());
    chooseLayout(layout);
    placeCentre(layout);
    if (layout.plan.pieces.isEmpty()) {
        return nullptr;
    }

    QList<Road> roads;
    layRoads(layout, roads);
    placeRulePieces(layout, roads);
    for (const Road &road : std::as_const(roads)) {
        placeAlong(layout, road);
    }
    for (const Road &road : std::as_const(roads)) {
        placeLamps(layout, road);
    }
    // After the pieces: the forecourts of pieces at diagonal roads are side road too.
    layout.plan.steps = QcVillageRoads::steps(layout.plan, layout.pathStep, layout.branchStep);

    QcVillagePlan &plan = layout.plan;
    plan.minX = plan.maxX = layout.centreX;
    plan.minZ = plan.maxZ = layout.centreZ;
    const QList<Rect> covered = layout.pieces + layout.roads;
    for (const Rect rect : covered) {
        plan.minX = std::min(plan.minX, rect.x0);
        plan.minZ = std::min(plan.minZ, rect.z0);
        plan.maxX = std::max(plan.maxX, rect.x1);
        plan.maxZ = std::max(plan.maxZ, rect.z1);
    }
    if (layout.style->overWater && layout.style->wetland.bottom != qcAirBlockId) {
        planWetland(layout);
    }
    squeeze(plan);
    return std::make_shared<const QcVillagePlan>(std::move(plan));
}

void QcVillagePlanner::placeCentre(Layout &layout) const
{
    // The door faces the main road (the axis next to it when that is diagonal).
    const QcVillageStyle::Blueprint &centre = layout.style->centre.first();
    const auto [fx, fz] = directions[std::size_t(layout.mainDirection / 2) * 2];
    const int turns = turnsFacing(fx, fz);
    const int sizeX = turns % 2 == 0 ? centre.structure()->sizeX() : centre.structure()->sizeZ();
    const int sizeZ = turns % 2 == 0 ? centre.structure()->sizeZ() : centre.structure()->sizeX();
    const Rect rect{layout.centreX - sizeX / 2, layout.centreZ - sizeZ / 2, layout.centreX - sizeX / 2 + sizeX - 1,
                    layout.centreZ - sizeZ / 2 + sizeZ - 1};
    const std::optional<int> floor = floorFor(layout, rect, m_settings.maxHeightSpread);
    if (!floor) {
        return;
    }

    addPiece(layout, centre, rect, {rect.x0, *floor, rect.z0}, turns);
    const QcStructurePlacement &placed = layout.plan.pieces.back();
    layout.plan.centre = centre.bell ? placed.toWorld(*centre.bell) : QcBlockPos{layout.centreX, *floor + 1, layout.centreZ};
}

void QcVillagePlanner::placeRulePieces(Layout &layout, const QList<Road> &roads) const
{
    const QList<QcVillageStyle::Piece> &pieces = layout.style->pieces;
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        const QcVillageStyle::Piece &piece = pieces[i];
        if (!piece.hasRules()) {
            continue;
        }
        // The chance is drawn for every kind with rules, so one kind's rules never shift another's.
        const bool lucky = random(layout) < piece.chance;
        if (!lucky || layout.plan.roadLength < piece.minRoadLength) {
            continue;
        }

        const int wanted = std::max(1, piece.max);
        while (layout.counts[i] < wanted) {
            const bool placed = piece.water > 0 ? placeAtWater(layout, roads, int(i)) : placeAnywhere(layout, roads, int(i));
            if (!placed) {
                break;
            }
            ++layout.counts[i];
        }
    }
}

bool QcVillagePlanner::placeAnywhere(Layout &layout, const QList<Road> &roads, int index) const
{
    for (int attempt = 0; attempt < ruleAttempts && !roads.isEmpty(); ++attempt) {
        const Road &road = roads[std::min(roads.size() - 1, qsizetype(random(layout) * double(roads.size())))];
        const int side = random(layout) < 0.5 ? -1 : 1;
        const int room = std::max(1, road.length - road.firstSlot - 3);
        const int along = road.firstSlot + 1 + int(random(layout) * room);
        if (placeAt(layout, road, {along, side}, index) > 0) {
            return true;
        }
    }
    return false;
}

bool QcVillagePlanner::placeAtWater(Layout &layout, const QList<Road> &roads, int index) const
{
    const QcVillageStyle::Piece &piece = layout.style->pieces[index];
    const qsizetype first = std::min(roads.size() - 1, qsizetype(random(layout) * double(roads.size())));
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
        const int keep = layout.style->overWater && layout.style->wetland.bottom != qcAirBlockId ? layout.style->wetland.reach() : 0;
        const int regionX0 = layout.regionX * m_spacing + keep;
        const int regionZ0 = layout.regionZ * m_spacing + keep;
        const int room = m_spacing - 2 * keep;
        const bool inRegion = jetty.x0 >= regionX0 && jetty.z0 >= regionZ0 && jetty.x1 < regionX0 + room && jetty.z1 < regionZ0 + room;
        if (!inRegion || std::ranges::any_of(layout.pieces, [&](Rect other) { return other.intersects(jetty, 0); })) {
            continue;
        }

        if (placeBesideEnd(layout, road, index)) {
            layout.plan.decks.append(*decks);
            markRoad(layout, {jetty.x0 + 2, jetty.z0 + 2, jetty.x1 - 2, jetty.z1 - 2});
            return true;
        }
    }
    return false;
}

bool QcVillagePlanner::placeBesideEnd(Layout &layout, const Road &road, int index) const
{
    // As near to the road's end as the building fits, on either side.
    const QcStructure &structure = *layout.style->pieces[index].first().structure();
    const int depth = std::max(structure.sizeX(), structure.sizeZ());
    for (int slot = 0; slot < waterSlots; ++slot) {
        const int along = road.length - depth - slot;
        for (const int side : {-1, 1}) {
            if (along > road.firstSlot && placeAt(layout, road, {along, side}, index) > 0) {
                return true;
            }
        }
    }
    return false;
}

void QcVillagePlanner::placeAlong(Layout &layout, const Road &road) const
{
    for (const int side : {-1, 1}) {
        if ((road.sides & (side < 0 ? 1 : 2)) == 0) {
            continue;
        }
        int along = road.firstSlot + 1;
        while (along < road.length - 2 && layout.plan.pieces.size() <= maxPieces) {
            const int reach = tryPiece(layout, road, along, side);
            if (reach < 0) {
                return;
            }
            along += reach > 0 ? reach + pieceGap : 1;
        }
    }
}

int QcVillagePlanner::tryPiece(Layout &layout, const Road &road, int along, int side) const
{
    const int index = choosePiece(layout);
    if (index < 0) {
        return -1;
    }
    const int reach = placeAt(layout, road, {along, side}, index);
    if (reach > 0) {
        ++layout.counts[index];
    }
    return reach;
}

int QcVillagePlanner::placeAt(Layout &layout, const Road &road, std::pair<int, int> alongAndSide, int index) const
{
    if (road.diagonal()) {
        return placeAtDiagonal(layout, road, alongAndSide, index);
    }
    const auto [along, side] = alongAndSide;
    // Away from the road on this side; the door faces back to it.
    const int px = -road.dz * side;
    const int pz = road.dx * side;
    const QcVillageStyle::Blueprint &blueprint = chooseBlueprint(layout, layout.style->pieces[index]);
    const QcStructure &structure = *blueprint.structure();
    const int turns = turnsFacing(-px, -pz);
    const int sizeX = turns % 2 == 0 ? structure.sizeX() : structure.sizeZ();
    const int sizeZ = turns % 2 == 0 ? structure.sizeZ() : structure.sizeX();
    const int alongSize = road.dx != 0 ? sizeX : sizeZ;
    const int acrossSize = road.dx != 0 ? sizeZ : sizeX;
    // Over water a piece's floor joins the boardwalk; on land a strip of ground lies between them.
    const int offset = layout.style->overWater ? roadHalfWidth + 1 : pieceOffset;
    const int a1 = along + alongSize - 1;
    const int c1 = offset + acrossSize - 1;
    const int xa = road.x + road.dx * along + px * offset;
    const int za = road.z + road.dz * along + pz * offset;
    const int xb = road.x + road.dx * a1 + px * c1;
    const int zb = road.z + road.dz * a1 + pz * c1;
    const Rect rect{std::min(xa, xb), std::min(za, zb), std::max(xa, xb), std::max(za, zb)};

    const bool hitsPiece = std::ranges::any_of(layout.pieces, [&](Rect other) { return rect.intersects(other, pieceGap); });
    // Over water the whole front of a piece lies on the boardwalk, so its door opens onto it.
    const bool pastEnd = layout.style->overWater && a1 >= road.length;
    if (pastEnd || !insideRadius(layout, rect) || hitsPiece || touchesRoad(layout, rect, offset - roadHalfWidth - 1)) {
        return 0;
    }
    const std::optional<int> floor = floorFor(layout, rect, m_settings.maxPieceSlope);
    if (!floor) {
        return 0;
    }

    addPiece(layout, blueprint, rect, {rect.x0, *floor, rect.z0}, turns);
    return alongSize;
}

int QcVillagePlanner::choosePiece(Layout &layout) const
{
    const QList<QcVillageStyle::Piece> &pieces = layout.style->pieces;
    const auto open = [&](qsizetype i) { return !pieces[i].hasRules() && (pieces[i].max == 0 || layout.counts[i] < pieces[i].max); };
    int total = 0;
    for (qsizetype i = 0; i < pieces.size(); ++i) {
        total += open(i) ? pieces[i].weight : 0;
    }
    if (total == 0) {
        return -1;
    }

    int pick = int(random(layout) * total);
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

void QcVillagePlanner::placeLamps(Layout &layout, const Road &road) const
{
    if (layout.style->lamp.blueprints.isEmpty()) {
        return;
    }
    const QcVillageStyle::Blueprint &lamp = layout.style->lamp.first();

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
            if (touchesRoad(layout, rect, 0)) {
                continue;
            }
            const bool free = std::ranges::none_of(layout.pieces, [&](Rect piece) { return rect.intersects(piece, pieceGap); });
            // Inside the radius like the pieces: a wide lamp never crosses the region's edge.
            const std::optional<int> floor = free && insideRadius(layout, rect) ? floorFor(layout, rect, 0) : std::nullopt;
            if (floor) {
                addPiece(layout, lamp, rect, {x, *floor, z}, 0);
            }
            break;
        }
    }
}

const QcVillageStyle::Blueprint &QcVillagePlanner::chooseBlueprint(Layout &layout, const QcVillageStyle::Piece &piece) const
{
    if (piece.blueprints.size() == 1) {
        return piece.first();
    }

    int total = 0;
    for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
        total += blueprint.weight;
    }
    int pick = int(random(layout) * total);
    for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
        if (pick < blueprint.weight) {
            return blueprint;
        }
        pick -= blueprint.weight;
    }
    return piece.blueprints.back();
}

void QcVillagePlanner::addPiece(Layout &layout, const QcVillageStyle::Blueprint &blueprint, Rect rect, QcBlockPos origin, int turns) const
{
    std::shared_ptr<const QcStructure> structure = blueprint.structure();
    QList<QcVillageSpot> spots = blueprint.spots;
    if (blueprint.variation.varies()) {
        structure = blueprint.variation.build(blueprint.variation.choose([this, &layout] { return variantRandom(layout); }));
        std::optional<QcBlockPos> bell;
        spots = m_styles.spotsOf(*structure, bell);
    }
    if (blueprint.fields && !layout.style->crops.isEmpty()) {
        structure = QcVillageFields::plant(*structure, layout.style->crops, m_content, [this, &layout] { return cropRandom(layout); });
    }

    layout.plan.pieces.append(QcStructurePlacement(structure, origin, turns, m_content));
    layout.pieces.append(rect);
    const QcStructurePlacement &placed = layout.plan.pieces.back();
    for (const QcVillageSpot &spot : std::as_const(spots)) {
        QList<QcVillagePlan::Spot> &list = spot.kind.isEmpty() ? layout.plan.beds : layout.plan.workstations;
        list.append(QcVillageSpot{placed.toWorld(spot.at), spot.kind});
    }
    for (const QcStructure::Mob &mob : structure->mobs) {
        layout.plan.mobs.append({mob.mob, placed.toWorld(mob.at)});
    }
    if (!layout.style->overWater) {
        addThresholds(layout, placed);
    }
}

void QcVillagePlanner::addThresholds(Layout &layout, const QcStructurePlacement &piece) const
{
    const QList<QcBlockPos> steps = doorsteps(piece);
    for (const QcBlockPos &at : steps) {
        if (!layout.roadCells.contains(cellKey(at.x, at.z))) {
            layout.roadCells.insert(cellKey(at.x, at.z));
            layout.plan.thresholds.append(at);
            layout.plan.branchRoads.append(at);
        }
    }
}

QList<QcBlockPos> QcVillagePlanner::doorsteps(const QcStructurePlacement &piece) const
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

bool QcVillagePlanner::insideRadius(const Layout &layout, Rect rect) const
{
    const int reach = m_settings.radius;
    return rect.x0 >= layout.centreX - reach && rect.x1 <= layout.centreX + reach && rect.z0 >= layout.centreZ - reach
           && rect.z1 <= layout.centreZ + reach;
}
