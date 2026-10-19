#include "world/QcVillageFixture.h"

#include "common/core/QcIntMath.h"
#include "worldgen/villages/QcVillageFields.h"
#include "worldgen/villages/QcVillageStyles.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Blocks between two buildings on one side of the road.
constexpr int pieceGap = 2;
// The road is three blocks wide around its middle line (QcVillageDraft::roadHalfWidth). On land a
// strip of ground lies between it and the buildings, over water they join the boardwalk
// (QcPiecePlacer).
constexpr int roadHalfWidth = 1;
constexpr int landOffset = roadHalfWidth + 2;
constexpr int waterOffset = roadHalfWidth + 1;

// One building of the fixture: its structure (with every optional part, crops planted) and its beds,
// workstations and bell in local cells.
struct Building
{
    std::shared_ptr<const QcStructure> structure;
    QList<QcVillageSpot> spots;
    std::optional<QcBlockPos> bell;
    bool fields = false;

    bool isHouse() const
    {
        return std::ranges::any_of(spots, [](const QcVillageSpot &spot) { return spot.kind.isEmpty(); });
    }
};

Building buildingOf(const QcVillageStyles &styles, const QcVillageStyle &style, const QcVillageStyle::Blueprint &blueprint,
                    const QcContentRegistry &content)
{
    Building building;
    building.structure = blueprint.variation.build(blueprint.variation.everything());
    QcVillageStyles::Spots spots = styles.spotsOf(*building.structure);
    building.spots = std::move(spots.spots);
    building.bell = spots.bell;
    building.fields = blueprint.fields;
    if (blueprint.fields && !style.crops.isEmpty()) {
        // The same crops every time: a fixed sequence instead of the seed.
        double next = 0.0;
        building.structure = QcVillageFields::plant(*building.structure, style.crops, content, [&next] {
            next += 0.37;
            next -= next >= 1.0 ? 1.0 : 0.0;
            return next;
        });
    }
    return building;
}

// The well first, then the lamp post and every blueprint of every kind once, the houses
// `houseCopies` times.
QList<Building> buildingsOf(const QcVillageStyles &styles, const QcVillageStyle &style, const QcContentRegistry &content, int houseCopies)
{
    QList<Building> buildings{buildingOf(styles, style, style.centre.first(), content)};
    if (!style.lamp.blueprints.isEmpty()) {
        buildings.append(buildingOf(styles, style, style.lamp.first(), content));
    }
    for (const QcVillageStyle::Piece &piece : style.pieces) {
        for (const QcVillageStyle::Blueprint &blueprint : piece.blueprints) {
            const Building building = buildingOf(styles, style, blueprint, content);
            const int copies = building.isHouse() ? std::max(1, houseCopies) : 1;
            for (int copy = 0; copy < copies; ++copy) {
                buildings.append(building);
            }
        }
    }
    return buildings;
}

// Adds a placed building to the plan: its piece, beds and workstations, fields and creatures.
void addBuilding(QcVillagePlan &plan, const Building &building, const QcStructurePlacement &placed, const QcContentRegistry &content)
{
    plan.pieces.append(placed);
    for (const QcVillageSpot &spot : building.spots) {
        QList<QcVillagePlan::Spot> &list = spot.kind.isEmpty() ? plan.beds : plan.workstations;
        list.append(QcVillageSpot{placed.toWorld(spot.at), spot.kind});
    }
    for (const QcStructure::Mob &mob : building.structure->mobs) {
        plan.mobs.append({mob.mob, placed.toWorld(mob.at)});
    }
    if (building.fields) {
        const QList<QcBlockPos> crops = QcVillageFields::cropCells(*building.structure, content);
        for (const QcBlockPos &cell : crops) {
            plan.fields.insert(placed.toWorld(cell));
        }
    }
}

// A cell of the road: on land a road cell, over water a boardwalk deck with a post on the style's grid.
void addRoadCell(QcVillagePlan &plan, const QcVillageStyle &style, QcBlockPos at)
{
    if (!style.overWater) {
        plan.roads.append(at);
        return;
    }
    const bool onGrid = qcFloorMod(at.x, style.stilts.spacing) == 0 && qcFloorMod(at.z, style.stilts.spacing) == 0;
    plan.decks.append({at, plan.pathBlock, onGrid ? style.stilts.post : qcAirBlockId});
}

// The road's frame: cells `along` the road from its start and `across` it (negative: to the north
// of a road running east, to the west of one running south), at the height of the road. The road
// runs east when the buildings of its first side are turned an even number of quarter turns.
struct Frame
{
    QcBlockPos road;
    int turns = 0;
    // On land, or a boardwalk over water.
    bool land = true;

    bool alongX() const { return turns % 2 == 0; }
    // Blocks from the road's middle line to the nearest cell of a building.
    int offset() const { return land ? landOffset : waterOffset; }
    QcBlockPos at(int along, int across) const
    {
        return alongX() ? QcBlockPos{road.x + along, road.y, road.z + across} : QcBlockPos{road.x + across, road.y, road.z + along};
    }
};

// The side of the road (-1 or 1 across it) of a building turned `turns` quarter turns: it faces the
// road (0: south, 1: west, 2: north, 3: east).
int sideOf(int turns)
{
    return turns == 0 || turns == 3 ? -1 : 1;
}

// On land the strip of ground between the road and a building is a side road, so the doorsteps
// join the road (QcPiecePlacer::addThresholds).
void addDoorsteps(QcVillagePlan &plan, const Frame &frame, int along, int length, int across)
{
    for (int a = along; a < along + length; ++a) {
        const QcBlockPos cell = frame.at(a, across);
        plan.branchRoads.append(cell);
        plan.thresholds.append(cell);
    }
}

// How far the buildings reach along each side of the road (the first side's buildings turned
// Options::turns, the second side's half a turn more) and away from it.
struct Sides
{
    int firstAlong = 1;
    int secondAlong = 1;
    int first = 0;
    int second = 0;
};

// The plan's surfaces and floor height from its style.
int prepare(QcVillagePlan &plan, const QcVillageStyle &style)
{
    plan.style = style.name;
    plan.layout = u"fixture"_s;
    plan.pathBlock = style.roads.front().block;
    plan.branchPathBlock = style.branchRoads.isEmpty() ? plan.pathBlock : style.branchRoads.front().block;
    plan.foundation = style.foundation;
    if (!style.overWater) {
        return QcVillageFixture::landFloor;
    }
    plan.stiltPost = style.stilts.post;
    plan.stiltSpacing = style.stilts.spacing;
    return QcVillageFixture::waterSurface + style.stilts.height;
}

// Places `building` with its front towards the road, from `along` on, on the side of the road its
// turns (`firstSide`: the frame's, else half a turn more) give, and adds it to the plan.
void placeBuilding(QcVillagePlan &plan, const Building &building, const Frame &frame, std::pair<int, bool> slot, const QcContentRegistry &content)
{
    const auto [along, firstSide] = slot;
    const int turns = firstSide ? frame.turns : (frame.turns + 2) % 4;
    const int side = sideOf(turns);
    // Along the road a building is as long as its front (local x), across it as deep as local z.
    const int length = building.structure->sizeX();
    const int nearest = side < 0 ? -frame.offset() - building.structure->sizeZ() + 1 : frame.offset();
    const QcStructurePlacement placed(building.structure, frame.at(along, nearest), turns, content);
    addBuilding(plan, building, placed, content);
    if (frame.land) {
        addDoorsteps(plan, frame, along, length, side * (frame.offset() - 1));
    }

    if (plan.pieces.size() == 1) {
        const QcBlockPos middle{building.structure->sizeX() / 2, 1, building.structure->sizeZ() / 2};
        plan.centre = placed.toWorld(building.bell.value_or(middle));
    }
}

// Places the buildings along the road: each on the side that reaches less far along it so far; the
// first one's bell (else its middle) is the meeting point.
Sides placeBuildings(QcVillagePlan &plan, const QList<Building> &buildings, const Frame &frame, const QcContentRegistry &content)
{
    Sides sides;
    for (const Building &building : buildings) {
        const bool firstSide = sides.firstAlong <= sides.secondAlong;
        int &along = firstSide ? sides.firstAlong : sides.secondAlong;
        placeBuilding(plan, building, frame, {along, firstSide}, content);
        along += building.structure->sizeX() + pieceGap;
        int &reach = firstSide ? sides.first : sides.second;
        reach = std::max(reach, building.structure->sizeZ());
    }
    return sides;
}

// The road (on land) or boardwalk (over water) and the extent of the plan around it.
void addRoad(QcVillagePlan &plan, const QcVillageStyle &style, const Frame &frame, Sides sides)
{
    const int end = std::max(sides.firstAlong, sides.secondAlong);
    for (int along = 0; along <= end; ++along) {
        for (int across = -roadHalfWidth; across <= roadHalfWidth; ++across) {
            addRoadCell(plan, style, frame.at(along, across));
        }
    }
    plan.roadLength = end + 1;
    plan.runs.append({frame.road.x, frame.road.z, frame.alongX() ? 1 : 0, frame.alongX() ? 0 : 1, plan.roadLength, 0, false});

    const int offset = frame.offset();
    const bool firstNegative = sideOf(frame.turns) < 0;
    const int negative = firstNegative ? sides.first : sides.second;
    const int positive = firstNegative ? sides.second : sides.first;
    const QcBlockPos low = frame.at(0, -offset - negative + 1);
    const QcBlockPos high = frame.at(end, offset + positive - 1);
    plan.minX = std::min(low.x, high.x);
    plan.maxX = std::max(low.x, high.x);
    plan.minZ = std::min(low.z, high.z);
    plan.maxZ = std::max(low.z, high.z);
}

// The turns of the `index`-th village laid out by spread.
int turnsOf(const QcVillageFixture::Options &base, qsizetype index)
{
    return base.turnEach ? base.turns + int(index) : base.turns;
}

} // namespace

namespace QcVillageFixture {

std::shared_ptr<QcVillagePlan> plan(const QcContentRegistry &content, const Options &options)
{
    const QcVillageStyles styles(content);
    const QcVillageStyle *style = styles.named(options.style);
    if (!style) {
        return nullptr;
    }

    auto plan = std::make_shared<QcVillagePlan>();
    plan->id = options.id;
    plan->alwaysLivedIn = options.alwaysLivedIn;
    const int floor = prepare(*plan, *style);
    const QList<Building> buildings = buildingsOf(styles, *style, content, options.houseCopies);
    const Frame frame{{options.x, floor, options.z}, qcFloorMod(options.turns, 4), !style->overWater};
    const Sides sides = placeBuildings(*plan, buildings, frame, content);
    addRoad(*plan, *style, frame, sides);
    return plan;
}

QList<std::shared_ptr<const QcVillagePlan>> spread(const QcContentRegistry &content, const QStringList &styles, int columns,
                                                   const Options &base)
{
    QList<std::shared_ptr<const QcVillagePlan>> villages;
    int bottom = base.z;
    for (qsizetype first = 0; first < styles.size(); first += columns) {
        // How far each village of the row reaches west and north of the start of its road; the row's
        // road lies far enough below the last row for its deepest village.
        const QStringList row = styles.mid(first, columns);
        QList<int> west;
        int north = 0;
        for (const QString &style : row) {
            Options probe = base;
            probe.style = style;
            probe.x = 0;
            probe.z = 0;
            probe.turns = turnsOf(base, first + west.size());
            const std::shared_ptr<QcVillagePlan> village = plan(content, probe);
            west.append(village ? -village->minX : 0);
            north = std::max(north, village ? -village->minZ : 0);
        }

        Options options = base;
        options.z = first == 0 ? base.z : bottom + villageGap + north;
        int left = base.x;
        for (qsizetype i = 0; i < row.size(); ++i) {
            options.style = row[i];
            options.id = base.id + first + i;
            options.x = left + west[i];
            options.turns = turnsOf(base, first + i);
            if (std::shared_ptr<QcVillagePlan> village = plan(content, options)) {
                left = village->maxX + villageGap + 1;
                bottom = std::max(bottom, village->maxZ);
                villages.append(std::move(village));
            }
        }
    }
    return villages;
}

QList<QcChunkPos> chunksOf(const QcVillagePlan &plan, int ring)
{
    const QcChunkPos low = QcBlockPos{plan.minX, 0, plan.minZ}.chunk();
    const QcChunkPos high = QcBlockPos{plan.maxX, 0, plan.maxZ}.chunk();
    QList<QcChunkPos> chunks;
    for (int cz = low.z - ring; cz <= high.z + ring; ++cz) {
        for (int cx = low.x - ring; cx <= high.x + ring; ++cx) {
            chunks.append({cx, cz});
        }
    }
    return chunks;
}

qsizetype upstairsBeds(const QcVillagePlan &plan)
{
    // A storey is at least three blocks: floor, two of room.
    const int floor = plan.pieces.isEmpty() ? 0 : plan.pieces.front().origin().y;
    return std::ranges::count_if(plan.beds, [floor](const QcVillagePlan::Spot &bed) { return bed.at.y >= floor + 3; });
}

Terrain::Terrain(std::shared_ptr<const QcContentRegistry> content, QList<std::shared_ptr<const QcVillagePlan>> villages)
    : m_content(std::move(content))
    , m_villages(std::move(villages))
{
    m_stone = m_content->blockId(u"qtcraft:stone"_s).value_or(qcAirBlockId);
    m_dirt = m_content->blockId(u"qtcraft:dirt"_s).value_or(m_stone);
    m_grass = m_content->blockId(u"qtcraft:grass_block"_s).value_or(m_dirt);
    m_water = m_content->blockId(u"qtcraft:water"_s).value_or(qcAirBlockId);
    m_spawn = m_villages.isEmpty() ? QcBlockPos{0, landFloor + 1, 0} : m_villages.front()->roads.value(0, m_villages.front()->centre);
    if (!m_villages.isEmpty() && m_villages.front()->roads.isEmpty() && !m_villages.front()->decks.isEmpty()) {
        m_spawn = m_villages.front()->decks.front().at;
    }
    m_spawn.y += 1;
}

bool Terrain::inLake(int x, int z) const
{
    return std::ranges::any_of(m_villages, [x, z](const std::shared_ptr<const QcVillagePlan> &plan) {
        return !plan->decks.isEmpty() && plan->covers(x, z, QcVillagePlan::reach);
    });
}

std::shared_ptr<QcChunk> Terrain::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    buildGround(*chunk);
    for (const std::shared_ptr<const QcVillagePlan> &plan : m_villages) {
        if (plan->overlaps(pos, 0)) {
            buildVillage(*chunk, *plan);
        }
    }
    return chunk;
}

void Terrain::buildGround(QcChunk &chunk) const
{
    for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
        for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
            buildColumn(chunk, lx, lz);
        }
    }
}

void Terrain::buildColumn(QcChunk &chunk, int lx, int lz) const
{
    const bool lake = inLake(chunk.pos().x * QcWorld::chunkSize + lx, chunk.pos().z * QcWorld::chunkSize + lz);
    const int top = lake ? lakeBottom : landFloor;
    for (int y = 0; y < top - 3; ++y) {
        chunk.setBlock(lx, y, lz, m_stone);
    }
    for (int y = std::max(0, top - 3); y < top; ++y) {
        chunk.setBlock(lx, y, lz, m_dirt);
    }
    chunk.setBlock(lx, top, lz, lake ? m_dirt : m_grass);
    for (int y = top + 1; lake && y <= waterSurface; ++y) {
        chunk.setBlock(lx, y, lz, m_water);
    }
}

void Terrain::buildVillage(QcChunk &chunk, const QcVillagePlan &plan) const
{
    for (const QcBlockPos &cell : plan.roads) {
        if (cell.chunk() == chunk.pos()) {
            chunk.setBlock(cell.localX(), cell.y, cell.localZ(), plan.pathBlock);
        }
    }
    for (const QcBlockPos &cell : plan.branchRoads) {
        if (cell.chunk() == chunk.pos()) {
            chunk.setBlock(cell.localX(), cell.y, cell.localZ(), plan.branchPathBlock);
        }
    }
    for (const QcVillagePlan::Deck &deck : plan.decks) {
        if (deck.at.chunk() == chunk.pos()) {
            chunk.setBlock(deck.at.localX(), deck.at.y, deck.at.localZ(), deck.block);
        }
        buildPost(chunk, deck.at, deck.post);
    }
    for (const QcStructurePlacement &piece : plan.pieces) {
        if (piece.overlaps(chunk.pos())) {
            piece.writeInto(chunk);
            buildStilts(chunk, piece, plan);
        }
    }
}

void Terrain::buildStilts(QcChunk &chunk, const QcStructurePlacement &piece, const QcVillagePlan &plan) const
{
    // Over water: posts every `stiltSpacing` cells and at the far edges (QcVillageGenerator).
    if (plan.stiltPost == qcAirBlockId) {
        return;
    }
    for (int dx = 0; dx < piece.sizeX(); ++dx) {
        for (int dz = 0; dz < piece.sizeZ(); ++dz) {
            const bool alongX = dx % plan.stiltSpacing == 0 || dx == piece.sizeX() - 1;
            const bool alongZ = dz % plan.stiltSpacing == 0 || dz == piece.sizeZ() - 1;
            if (alongX && alongZ) {
                buildPost(chunk, piece.origin().offset(dx, 0, dz), plan.stiltPost);
            }
        }
    }
}

void Terrain::buildPost(QcChunk &chunk, QcBlockPos floor, QcBlockId post) const
{
    if (post == qcAirBlockId || floor.chunk() != chunk.pos()) {
        return;
    }
    for (int y = floor.y - 1; y > lakeBottom; --y) {
        chunk.setBlock(floor.localX(), y, floor.localZ(), post);
    }
}

QcBlockPos Terrain::spawnPoint() const
{
    return m_spawn;
}

std::shared_ptr<const QcVillagePlan> Terrain::villageFor(QcChunkPos pos) const
{
    for (const std::shared_ptr<const QcVillagePlan> &plan : m_villages) {
        if (plan->overlaps(pos, 0)) {
            return plan;
        }
    }
    return nullptr;
}

std::shared_ptr<const QcVillagePlan> Terrain::nearestVillage(QcBlockPos from, int reach) const
{
    std::shared_ptr<const QcVillagePlan> nearest;
    qint64 best = qint64(reach) * reach;
    for (const std::shared_ptr<const QcVillagePlan> &plan : m_villages) {
        const qint64 dx = plan->centre.x - from.x;
        const qint64 dz = plan->centre.z - from.z;
        if (dx * dx + dz * dz <= best) {
            best = dx * dx + dz * dz;
            nearest = plan;
        }
    }
    return nearest;
}

} // namespace QcVillageFixture
