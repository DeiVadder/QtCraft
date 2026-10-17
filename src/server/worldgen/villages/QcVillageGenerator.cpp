#include "QcVillageGenerator.h"

#include <algorithm>
#include <cmath>

namespace {

// The terrain above a piece's floor is cleared this far above its top.
constexpr int clearance = 6;
// Foundations reach at most this far down, jetty posts this far.
constexpr int maxFoundation = 16;
constexpr int maxPost = 24;
// Above a doorstep the terrain is cleared this high.
constexpr int thresholdClearance = 3;

int regionOf(int block, int spacing)
{
    return qcFloorDiv(block, spacing);
}

} // namespace

QcVillageGenerator::QcVillageGenerator(const QcTerrainGenerator &terrain, std::shared_ptr<const QcContentRegistry> content, quint64 seed,
                                       QStringList *warnings)
    : m_content(std::move(content))
    , m_styles(*m_content, warnings)
    , m_planner(terrain, *m_content, m_styles, seed)
    , m_wetland(terrain, seed)
{
}

std::shared_ptr<const QcVillagePlan> QcVillageGenerator::regionPlan(int regionX, int regionZ) const
{
    return m_cache.plan(regionX, regionZ, [this](int x, int z) { return m_planner.plan(x, z); });
}

std::shared_ptr<const QcVillagePlan> QcVillageGenerator::planFor(QcChunkPos chunk) const
{
    const int spacing = m_planner.spacing();
    return regionPlan(regionOf(chunk.x * QcWorld::chunkSize, spacing), regionOf(chunk.z * QcWorld::chunkSize, spacing));
}

std::shared_ptr<const QcVillagePlan> QcVillageGenerator::nearest(QcBlockPos from, int reach) const
{
    const int spacing = m_planner.spacing();
    const int regionX = regionOf(from.x, spacing);
    const int regionZ = regionOf(from.z, spacing);
    const int rings = reach / spacing + 1;
    std::shared_ptr<const QcVillagePlan> best;
    double bestDistance = double(reach);
    for (int dz = -rings; dz <= rings; ++dz) {
        for (int dx = -rings; dx <= rings; ++dx) {
            std::shared_ptr<const QcVillagePlan> plan = regionPlan(regionX + dx, regionZ + dz);
            if (!plan) {
                continue;
            }
            const double distance = std::hypot(double(plan->centre.x - from.x), double(plan->centre.z - from.z));
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = std::move(plan);
            }
        }
    }
    return best;
}

bool QcVillageGenerator::isGround(QcBlockId block) const
{
    const QcBlockDefinition &definition = m_content->block(block);
    return block != qcAirBlockId && definition.solid && !definition.liquid && !definition.replaceable;
}

void QcVillageGenerator::buildPiece(QcChunk &chunk, const QcStructurePlacement &piece, const QcVillagePlan &plan) const
{
    const int chunkX = chunk.pos().x * QcWorld::chunkSize;
    const int chunkZ = chunk.pos().z * QcWorld::chunkSize;
    const QcBlockPos origin = piece.origin();
    // Over water the pieces stand on stilts instead of a foundation.
    const QcBlockId foundation = plan.stiltPost == qcAirBlockId ? plan.foundation : qcAirBlockId;
    const int x0 = std::max(chunkX, origin.x);
    const int x1 = std::min(chunkX + QcWorld::chunkSize, origin.x + piece.sizeX());
    const int z0 = std::max(chunkZ, origin.z);
    const int z1 = std::min(chunkZ + QcWorld::chunkSize, origin.z + piece.sizeZ());
    for (int x = x0; x < x1; ++x) {
        for (int z = z0; z < z1; ++z) {
            prepareColumn(chunk, {x - chunkX, 0, z - chunkZ}, piece, foundation);
        }
    }
    piece.writeInto(chunk);
    if (foundation != qcAirBlockId) {
        return;
    }

    for (int x = x0; x < x1; ++x) {
        for (int z = z0; z < z1; ++z) {
            if (isStiltColumn(piece, x, z, plan.stiltSpacing)) {
                buildPost(chunk, {x - chunkX, origin.y, z - chunkZ}, plan.stiltPost);
            }
        }
    }
}

bool QcVillageGenerator::isStiltColumn(const QcStructurePlacement &piece, int x, int z, int spacing)
{
    const int dx = x - piece.origin().x;
    const int dz = z - piece.origin().z;
    const bool alongX = dx % spacing == 0 || dx == piece.sizeX() - 1;
    const bool alongZ = dz % spacing == 0 || dz == piece.sizeZ() - 1;
    return alongX && alongZ;
}

void QcVillageGenerator::buildPost(QcChunk &chunk, QcBlockPos floor, QcBlockId post) const
{
    // Only under a floor block the piece placed (not under a cell it keeps or leaves open).
    if (!isGround(chunk.block(floor.x, floor.y, floor.z))) {
        return;
    }
    for (int y = floor.y - 1; y > 0 && y >= floor.y - maxPost && !isGround(chunk.block(floor.x, y, floor.z)); --y) {
        chunk.setBlock(floor.x, y, floor.z, post);
    }
}

void QcVillageGenerator::prepareColumn(QcChunk &chunk, QcBlockPos local, const QcStructurePlacement &piece, QcBlockId foundation) const
{
    const QcBlockPos origin = piece.origin();
    const int top = std::min(QcWorld::worldHeight - 1, origin.y + piece.structure().sizeY() + clearance);
    for (int y = std::max(0, origin.y + 1); y <= top; ++y) {
        if (chunk.block(local.x, y, local.z) != qcAirBlockId) {
            chunk.setBlock(local.x, y, local.z, qcAirBlockId);
        }
    }
    for (int y = origin.y - 1; foundation != qcAirBlockId && y > 0 && y >= origin.y - maxFoundation && !isGround(chunk.block(local.x, y, local.z));
         --y) {
        chunk.setBlock(local.x, y, local.z, foundation);
    }
}

void QcVillageGenerator::buildThresholds(QcChunk &chunk, const QList<QcBlockPos> &thresholds, QcBlockId foundation) const
{
    const QcChunkPos pos = chunk.pos();
    for (const QcBlockPos &cell : thresholds) {
        if (cell.chunk() != pos || cell.y <= 0 || cell.y + thresholdClearance >= QcWorld::worldHeight) {
            continue;
        }
        const int lx = cell.localX();
        const int lz = cell.localZ();
        for (int y = cell.y + 1; y <= cell.y + thresholdClearance; ++y) {
            chunk.setBlock(lx, y, lz, qcAirBlockId);
        }
        for (int y = cell.y; y > 0 && y >= cell.y - maxFoundation && !isGround(chunk.block(lx, y, lz)); --y) {
            chunk.setBlock(lx, y, lz, foundation);
        }
    }
}

void QcVillageGenerator::buildRoads(QcChunk &chunk, const QList<QcBlockPos> &cells, QcBlockId path) const
{
    const QcChunkPos pos = chunk.pos();
    for (const QcBlockPos &cell : cells) {
        if (cell.chunk() != pos || cell.y <= 0 || cell.y + 2 >= QcWorld::worldHeight) {
            continue;
        }
        const int lx = cell.localX();
        const int lz = cell.localZ();
        // A cell levelled to the road's middle line lies one block above the ground (the path fills
        // up to it) or one below (the ground above is cut away).
        const bool raised = !isGround(chunk.block(lx, cell.y, lz)) && isGround(chunk.block(lx, cell.y - 1, lz));
        if (!isGround(chunk.block(lx, cell.y, lz)) && !raised) {
            continue;
        }
        chunk.setBlock(lx, cell.y, lz, path);
        for (int y = cell.y + 1; y <= cell.y + 2; ++y) {
            if (chunk.block(lx, y, lz) != qcAirBlockId) {
                chunk.setBlock(lx, y, lz, qcAirBlockId);
            }
        }
    }
}

void QcVillageGenerator::buildSteps(QcChunk &chunk, const QList<QcVillagePlan::Step> &steps) const
{
    const QcChunkPos pos = chunk.pos();
    for (const QcVillagePlan::Step &step : steps) {
        const QcBlockPos at = step.at;
        // On the road's top block only, which buildRoads() cleared above.
        if (at.chunk() == pos && at.y > 0 && at.y < QcWorld::worldHeight && chunk.block(at.localX(), at.y, at.localZ()) == qcAirBlockId
            && isGround(chunk.block(at.localX(), at.y - 1, at.localZ()))) {
            chunk.setBlock(at.localX(), at.y, at.localZ(), step.block);
        }
    }
}

void QcVillageGenerator::buildDecks(QcChunk &chunk, const QList<QcVillagePlan::Deck> &decks) const
{
    const QcChunkPos pos = chunk.pos();
    for (const QcVillagePlan::Deck &deck : decks) {
        const QcBlockPos at = deck.at;
        if (at.chunk() != pos || at.y <= 0 || at.y + 2 >= QcWorld::worldHeight) {
            continue;
        }
        const int lx = at.localX();
        const int lz = at.localZ();
        chunk.setBlock(lx, at.y, lz, deck.block);
        for (int y = at.y + 1; y <= at.y + 2; ++y) {
            if (chunk.block(lx, y, lz) != qcAirBlockId) {
                chunk.setBlock(lx, y, lz, qcAirBlockId);
            }
        }
        if (deck.post == qcAirBlockId) {
            continue;
        }
        for (int y = at.y - 1; y > 0 && y >= at.y - maxPost && !isGround(chunk.block(lx, y, lz)); --y) {
            chunk.setBlock(lx, y, lz, deck.post);
        }
    }
}

void QcVillageGenerator::build(QcChunk &chunk, const QcVillagePlan &plan) const
{
    if (!plan.overlaps(chunk.pos(), 0)) {
        return;
    }

    m_wetland.shape(chunk, plan);
    buildThresholds(chunk, plan.thresholds, plan.foundation);
    // Where a branch meets its road, the road from the centre keeps its surface.
    buildRoads(chunk, plan.branchRoads, plan.branchPathBlock);
    buildRoads(chunk, plan.roads, plan.pathBlock);
    buildSteps(chunk, plan.steps);
    buildDecks(chunk, plan.decks);
    for (const QcStructurePlacement &piece : plan.pieces) {
        if (piece.overlaps(chunk.pos())) {
            buildPiece(chunk, piece, plan);
        }
    }
}
