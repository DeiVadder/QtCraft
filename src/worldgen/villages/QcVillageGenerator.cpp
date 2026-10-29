// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/villages/QcVillageGenerator.h"

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

// The blocks the biomes freeze their water to at the sea level (ice), and the terrain's ice where
// water freezes by temperature (decision E-858).
QList<QcBlockId> frozenWaterOf(const QcContentRegistry &content)
{
    QList<QcBlockId> blocks;
    QStringList names{content.worldgen().terrainSettings().coldEdges.iceBlock};
    for (const QcBiomeDefinition &biome : content.worldgen().biomes()) {
        names.append(biome.frozenWaterBlock);
    }
    for (const QString &name : names) {
        const QcBlockId block = content.blockId(name).value_or(qcAirBlockId);
        if (block != qcAirBlockId && !blocks.contains(block)) {
            blocks.append(block);
        }
    }
    return blocks;
}

} // namespace

QcVillageGenerator::QcVillageGenerator(const QcTerrainGenerator &terrain, std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_content(std::move(content))
    , m_styles(*m_content)
    , m_planner(terrain, *m_content, m_styles, seed)
    , m_wetland(terrain, seed)
    , m_centreBlend(terrain)
    , m_frozenWater(frozenWaterOf(*m_content))
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

QcChunkFurnishing QcVillageGenerator::furnishingIn(QcChunkPos pos, const QcVillagePlan &plan)
{
    QcChunkFurnishing furnishing;
    for (const QcStructurePlacement &piece : plan.pieces) {
        if (piece.structure().loot.isEmpty() || !piece.overlaps(pos)) {
            continue;
        }
        for (const QcStructureDefinition::Loot &loot : piece.structure().loot) {
            const QcBlockPos at = piece.toWorld(loot.at);
            if (at.chunk() == pos) {
                furnishing.chests.append({at, loot.table, static_cast<quint64>(qHashMulti(0x403u, at.x, at.y, at.z))});
            }
        }
    }
    return furnishing;
}

QcStructureSites QcVillageGenerator::sitesNear(QcBlockPos from, int reach) const
{
    return collectSites(from, reach, true).value_or(QcStructureSites{});
}

std::optional<QcStructureSites> QcVillageGenerator::plannedSitesNear(QcBlockPos from, int reach) const
{
    return collectSites(from, reach, false);
}

std::optional<QcStructureSites> QcVillageGenerator::collectSites(QcBlockPos from, int reach, bool mayPlan) const
{
    const int spacing = m_planner.spacing();
    QcStructureSites sites;
    for (int rz = regionOf(from.z - reach, spacing); rz <= regionOf(from.z + reach, spacing); ++rz) {
        for (int rx = regionOf(from.x - reach, spacing); rx <= regionOf(from.x + reach, spacing); ++rx) {
            const std::optional<std::shared_ptr<const QcVillagePlan>> plan = mayPlan ? regionPlan(rx, rz) : m_cache.planned(rx, rz);
            if (!plan) {
                return std::nullopt;
            }
            if (*plan) {
                sites.add(sitesOf(**plan));
            }
        }
    }
    return sites;
}

QcStructureSites QcVillageGenerator::sitesOf(const QcVillagePlan &plan)
{
    QcStructureSites sites;
    for (const QcStructurePlacement &piece : plan.pieces) {
        if (piece.structure().hasSites()) {
            sites.add(QcStructureSites::of(piece));
        }
    }
    return sites;
}

std::shared_ptr<const QcVillagePlan> QcVillageGenerator::nearest(QcBlockPos from, int reach) const
{
    const int spacing = m_planner.spacing();
    const int regionX = regionOf(from.x, spacing);
    const int regionZ = regionOf(from.z, spacing);
    const int rings = reach / spacing + 1;
    std::shared_ptr<const QcVillagePlan> best;
    double bestDistance = static_cast<double>(reach);
    for (int dz = -rings; dz <= rings; ++dz) {
        for (int dx = -rings; dx <= rings; ++dx) {
            std::shared_ptr<const QcVillagePlan> plan = regionPlan(regionX + dx, regionZ + dz);
            if (!plan) {
                continue;
            }
            const double distance = std::hypot(static_cast<double>(plan->centre.x - from.x), static_cast<double>(plan->centre.z - from.z));
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = std::move(plan);
            }
        }
    }
    return best;
}

std::optional<QcStructurePlacement> QcVillageGenerator::nearestPiece(const QString &name, QcBlockPos from, int reach) const
{
    const int spacing = m_planner.spacing();
    QList<QcStructurePlacement> pieces;
    for (int rz = regionOf(from.z - reach, spacing); rz <= regionOf(from.z + reach, spacing); ++rz) {
        for (int rx = regionOf(from.x - reach, spacing); rx <= regionOf(from.x + reach, spacing); ++rx) {
            if (const std::shared_ptr<const QcVillagePlan> plan = regionPlan(rx, rz)) {
                pieces.append(plan->pieces);
            }
        }
    }

    std::optional<QcStructurePlacement> best;
    double bestDistance = static_cast<double>(reach);
    for (const QcStructurePlacement &piece : std::as_const(pieces)) {
        const double distance = std::hypot(piece.origin().x + piece.sizeX() / 2.0 - from.x, piece.origin().z + piece.sizeZ() / 2.0 - from.z);
        if (piece.structure().name() == name && distance <= bestDistance) {
            bestDistance = distance;
            best = piece;
        }
    }
    return best;
}

bool QcVillageGenerator::isGround(QcBlockId block) const
{
    const QcBlockDefinition &definition = m_content->block(block);
    // Frozen water is a thin sheet over the water (decision E-815): posts and foundations reach
    // through it to the lake bed, so nothing hangs in the air once the ice breaks or melts.
    return block != qcAirBlockId && definition.solid && !definition.liquid && !definition.replaceable && !m_frozenWater.contains(block);
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
        // up to it) or one below (the ground above is cut away). A cut cell in a cave just under the
        // ground (the blended ground around a centre can lower the ground onto a cave's roof) gets
        // its path too: it closes the cave's top instead of leaving the ground above the road.
        const bool raised = !isGround(chunk.block(lx, cell.y, lz)) && isGround(chunk.block(lx, cell.y - 1, lz));
        const bool cut = isGround(chunk.block(lx, cell.y + 1, lz));
        if (!isGround(chunk.block(lx, cell.y, lz)) && !raised && !cut) {
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
    // The ground around the centre blended to its floor before the roads, which lie on it (decision E-893).
    if (plan.centreBlend > 0) {
        m_centreBlend.shape(chunk, plan.pieces.front(), plan.centreBlend);
    }
    buildThresholds(chunk, plan.thresholds, plan.foundation);
    // Where a branch meets its road, the road from the centre keeps its surface.
    buildRoads(chunk, plan.branchRoads, plan.branchPathBlock);
    buildRoads(chunk, plan.roads, plan.pathBlock);
    buildSteps(chunk, plan.steps);
    buildDecks(chunk, plan.decks);
    buildDecks(chunk, plan.bridges);
    for (const QcStructurePlacement &piece : plan.pieces) {
        if (piece.overlaps(chunk.pos())) {
            buildPiece(chunk, piece, plan);
        }
    }
}
