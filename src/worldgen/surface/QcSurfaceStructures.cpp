// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/surface/QcSurfaceStructures.h"

#include "worldgen/villages/QcVillageGenerator.h"

#include <algorithm>
#include <cmath>

namespace {

int floorDiv(int value, int divisor)
{
    return static_cast<int>(std::floor(static_cast<double>(value) / divisor));
}

// The regions of size `spacing` that the columns x0..maxX, z0..maxZ reach into.
QList<std::pair<int, int>> regionsOver(QcXzRect columns, int spacing)
{
    QList<std::pair<int, int>> regions;
    for (int rz = floorDiv(columns.minZ, spacing); rz <= floorDiv(columns.maxZ, spacing); ++rz) {
        for (int rx = floorDiv(columns.minX, spacing); rx <= floorDiv(columns.maxX, spacing); ++rx) {
            regions.append({rx, rz});
        }
    }
    return regions;
}

QcXzRect grown(QcXzRect rect, int margin)
{
    return {rect.minX - margin, rect.minZ - margin, rect.maxX + margin, rect.maxZ + margin};
}

QcXzRect chunkRect(QcChunkPos pos)
{
    const int x = pos.x * QcWorld::chunkSize;
    const int z = pos.z * QcWorld::chunkSize;
    return {x, z, x + QcWorld::chunkSize - 1, z + QcWorld::chunkSize - 1};
}

} // namespace

QcSurfaceStructures::PlanKey QcSurfaceStructures::planKey(QcSurfacePlanner::KindId kind, int regionX, int regionZ)
{
    return {kind, static_cast<qint64>((static_cast<quint64>(static_cast<quint32>(regionX)) << 32) | static_cast<quint32>(regionZ))};
}

QcSurfaceStructures::QcSurfaceStructures(const QcTerrainGenerator &terrain, const QcVillageGenerator &villages, const QcContentRegistry &content,
                                         quint64 seed)
    : m_villages(villages)
    , m_planner(terrain, content, seed)
    , m_blend(terrain)
{
    m_ground.resize(static_cast<std::size_t>(content.stateCount()));
    for (std::size_t id = 0; id < m_ground.size(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        m_ground[id] = block.solid && !block.liquid;
    }
}

std::shared_ptr<const QcStructurePlacement> QcSurfaceStructures::planOf(QcSurfacePlanner::KindId kind, int regionX, int regionZ) const
{
    const PlanKey key = planKey(kind, regionX, regionZ);
    {
        const std::scoped_lock lock(m_mutex);
        if (const auto it = m_plans.constFind(key); it != m_plans.cend()) {
            return *it;
        }
    }
    // Planned outside the lock: planning asks the villages and the kinds before this one.
    const auto occupied = [this](QcSurfacePlanner::KindId planned, QcXzRect footprint, int margin) { return this->occupied(planned, footprint, margin); };
    std::optional<QcStructurePlacement> placement = planner().plan(kind, regionX, regionZ, occupied);
    const std::shared_ptr<const QcStructurePlacement> plan =
        placement ? std::make_shared<const QcStructurePlacement>(std::move(*placement)) : nullptr;
    const std::scoped_lock lock(m_mutex);
    if (m_plans.size() >= cacheCapacity) {
        m_plans.clear();
    }
    return *m_plans.insert(key, plan);
}

std::optional<std::shared_ptr<const QcStructurePlacement>> QcSurfaceStructures::plannedOf(QcSurfacePlanner::KindId kind, int regionX, int regionZ) const
{
    const std::scoped_lock lock(m_mutex);
    const auto it = m_plans.constFind(planKey(kind, regionX, regionZ));
    return it == m_plans.cend() ? std::nullopt : std::optional(*it);
}

bool QcSurfaceStructures::occupied(QcSurfacePlanner::KindId kind, QcXzRect footprint, int margin) const
{
    // Blended ground (decision E-889) never reaches another footprint, either way: the gap is at
    // least the larger blend, even where `villageMargin` is smaller.
    const int blend = planner().kinds()[kind].rule.blend;

    // Villages: every region of their spacing the footprint and its margin reach into.
    const int villageGap = std::max(margin, blend);
    const QList<std::pair<int, int>> villageRegions = regionsOver(grown(footprint, villageGap), m_villages.spacing());
    for (const auto &[rx, rz] : villageRegions) {
        const std::shared_ptr<const QcVillagePlan> village = m_villages.regionPlan(rx, rz);
        if (village && footprint.intersects({village->minX, village->minZ, village->maxX, village->maxZ}, villageGap)) {
            return true;
        }
    }

    // The kinds before this one, each in the regions of its own spacing.
    for (const QcSurfacePlanner::KindId other : planner().kinds().ids()) {
        if (other >= kind) {
            break;
        }
        const QcStructureDefinition::Surface &rule = planner().kinds()[other].rule;
        // Two blends must not overlap either, so their widths add up.
        const int gap = std::max(margin, blend + rule.blend);
        const QList<std::pair<int, int>> regions = regionsOver(grown(footprint, gap), rule.spacing);
        for (const auto &[rx, rz] : regions) {
            const std::shared_ptr<const QcStructurePlacement> plan = planOf(other, rx, rz);
            if (plan && footprint.intersects(QcSurfacePlanner::footprintOf(*plan), gap)) {
                return true;
            }
        }
    }
    return false;
}

QList<std::shared_ptr<const QcStructurePlacement>> QcSurfaceStructures::plansTouching(QcChunkPos pos) const
{
    // A copy and its blended ground (decision E-889) lie wholly in its region: the regions of each
    // kind the chunk reaches into.
    QList<std::shared_ptr<const QcStructurePlacement>> plans;
    const QcXzRect chunk = chunkRect(pos);
    for (const QcSurfacePlanner::KindId kind : planner().kinds().ids()) {
        const QcSurfacePlanner::Kind &type = planner().kinds()[kind];
        const QList<std::pair<int, int>> regions = regionsOver(chunk, type.rule.spacing);
        for (const auto &[rx, rz] : regions) {
            std::shared_ptr<const QcStructurePlacement> plan = planOf(kind, rx, rz);
            if (plan && chunk.intersects(QcSurfacePlanner::footprintOf(*plan), type.rule.blend)) {
                plans.append(std::move(plan));
            }
        }
    }
    return plans;
}

QList<QcXzRect> QcSurfaceStructures::footprintsNear(QcChunkPos pos, int margin) const
{
    // A footprint with its blended ground (decision E-889): nothing is built on ground that moves.
    return grownFootprintsNear(pos, margin, false);
}

QList<QcXzRect> QcSurfaceStructures::bareNear(QcChunkPos pos, int margin) const
{
    return grownFootprintsNear(pos, margin, true);
}

QList<QcXzRect> QcSurfaceStructures::grownFootprintsNear(QcChunkPos pos, int margin, bool plantsOnBlend) const
{
    QList<QcXzRect> footprints;
    const QcXzRect chunk = chunkRect(pos);
    for (const QcSurfacePlanner::KindId kind : planner().kinds().ids()) {
        const QcStructureDefinition::Surface &rule = planner().kinds()[kind].rule;
        // With plants on the blend two columns stay bare beside the walls: no leaves and no bush right
        // before an opening.
        const int around = plantsOnBlend && rule.blendVegetation ? 2 : rule.blend;
        const QList<std::pair<int, int>> regions = regionsOver(grown(chunk, margin), rule.spacing);
        for (const auto &[rx, rz] : regions) {
            const std::shared_ptr<const QcStructurePlacement> plan = planOf(kind, rx, rz);
            const QcXzRect footprint = plan ? grown(QcSurfacePlanner::footprintOf(*plan), around) : QcXzRect{};
            if (plan && chunk.intersects(footprint, margin)) {
                footprints.append(footprint);
            }
        }
    }
    return footprints;
}

void QcSurfaceStructures::blend(QcChunk &chunk) const
{
    const QList<std::shared_ptr<const QcStructurePlacement>> plans = plansTouching(chunk.pos());
    for (const std::shared_ptr<const QcStructurePlacement> &plan : plans) {
        if (const std::optional<QcSurfacePlanner::KindId> kind = planner().kindIndex(plan->structure().name())) {
            m_blend.shape(chunk, *plan, planner().kinds()[*kind].rule.blend);
        }
    }
}

QList<QcSurfaceStructures::Moved> QcSurfaceStructures::blendedGround(const QcTerrainColumnGrid &columns, QcChunkPos pos) const
{
    QList<Moved> moved;
    const QcXzRect grid = grown(chunkRect(pos), QcTerrainColumnGrid::margin);
    for (const QcSurfacePlanner::KindId kind : planner().kinds().ids()) {
        const QcStructureDefinition::Surface &rule = planner().kinds()[kind].rule;
        const QList<std::pair<int, int>> regions = regionsOver(grid, rule.spacing);
        for (const auto &[rx, rz] : regions) {
            const std::shared_ptr<const QcStructurePlacement> plan = rule.blend > 0 ? planOf(kind, rx, rz) : nullptr;
            if (plan) {
                moved.append(movedAround(*plan, rule.blend, columns, grid));
            }
        }
    }
    return moved;
}

QList<QcSurfaceStructures::Moved> QcSurfaceStructures::movedAround(const QcStructurePlacement &plan, int blend, const QcTerrainColumnGrid &columns,
                                                                   QcXzRect grid) const
{
    QList<Moved> moved;
    const QcXzRect ground = grown(QcSurfacePlanner::footprintOf(plan), blend);
    for (int z = std::max(ground.minZ, grid.minZ); z <= std::min(ground.maxZ, grid.maxZ); ++z) {
        for (int x = std::max(ground.minX, grid.minX); x <= std::min(ground.maxX, grid.maxX); ++x) {
            if (const std::optional<int> top = m_blend.topAt(plan, blend, x, z, columns.at(x, z).height - 1)) {
                moved.append({x, z, *top});
            }
        }
    }
    return moved;
}

void QcSurfaceStructures::build(QcChunk &chunk) const
{
    if (planner().kinds().isEmpty()) {
        return;
    }
    const QList<std::shared_ptr<const QcStructurePlacement>> plans = plansTouching(chunk.pos());
    for (const std::shared_ptr<const QcStructurePlacement> &plan : plans) {
        const std::optional<QcSurfacePlanner::KindId> kind = planner().kindIndex(plan->structure().name());
        if (kind) {
            build(chunk, *plan, planner().kinds()[*kind]);
        }
    }
}

void QcSurfaceStructures::build(QcChunk &chunk, const QcStructurePlacement &placement, const QcSurfacePlanner::Kind &kind) const
{
    const QcBlockId foundation = kind.foundation;
    const int clearFrom = kind.rule.clearFrom;
    const int chunkX = chunk.pos().x * QcWorld::chunkSize;
    const int chunkZ = chunk.pos().z * QcWorld::chunkSize;
    const QcBlockPos origin = placement.origin();
    const int top = std::min(QcWorld::worldHeight - 1, origin.y + placement.structure().sizeY() + clearance);
    const int x0 = std::max(chunkX, origin.x);
    const int x1 = std::min(chunkX + QcWorld::chunkSize, origin.x + placement.sizeX());
    const int z0 = std::max(chunkZ, origin.z);
    const int z1 = std::min(chunkZ + QcWorld::chunkSize, origin.z + placement.sizeZ());
    for (int x = x0; x < x1; ++x) {
        for (int z = z0; z < z1; ++z) {
            const int lx = x - chunkX;
            const int lz = z - chunkZ;
            for (int y = origin.y + clearFrom; kind.rule.clearTerrain && y <= top; ++y) {
                chunk.setBlock(lx, y, lz, qcAirBlockId);
            }
            for (int y = origin.y - 1; foundation != qcAirBlockId && y > 0 && y >= origin.y - maxFoundation && !m_ground[chunk.block(lx, y, lz)]; --y) {
                chunk.setBlock(lx, y, lz, foundation);
            }
        }
    }
    placement.writeInto(chunk);
}

QcChunkFurnishing QcSurfaceStructures::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    if (planner().kinds().isEmpty()) {
        return furnishing;
    }
    const QList<std::shared_ptr<const QcStructurePlacement>> plans = plansTouching(pos);
    for (const std::shared_ptr<const QcStructurePlacement> &plan : plans) {
        for (const QcStructureDefinition::Loot &loot : plan->structure().loot) {
            const QcBlockPos at = plan->toWorld(loot.at);
            if (at.chunk() == pos) {
                furnishing.chests.append({at, loot.table, static_cast<quint64>(qHashMulti(0x590u, at.x, at.y, at.z))});
            }
        }
    }
    return furnishing;
}

QcStructureSites QcSurfaceStructures::sitesNear(QcBlockPos from, int reach) const
{
    return collectSites(from, reach, true).value_or(QcStructureSites{});
}

std::optional<QcStructureSites> QcSurfaceStructures::plannedSitesNear(QcBlockPos from, int reach) const
{
    return collectSites(from, reach, false);
}

std::optional<QcStructureSites> QcSurfaceStructures::collectSites(QcBlockPos from, int reach, bool mayPlan) const
{
    QcStructureSites sites;
    const QcXzRect area{from.x - reach, from.z - reach, from.x + reach, from.z + reach};
    for (const QcSurfacePlanner::KindId kind : planner().kinds().ids()) {
        if (!planner().kinds()[kind].structure()->hasSites()) {
            continue;
        }
        const QList<std::pair<int, int>> regions = regionsOver(area, planner().kinds()[kind].rule.spacing);
        for (const auto &[rx, rz] : regions) {
            const std::optional<std::shared_ptr<const QcStructurePlacement>> plan = mayPlan ? planOf(kind, rx, rz)
                : plannedOf(kind, rx, rz);
            if (!plan) {
                return std::nullopt;
            }
            if (*plan) {
                sites.add(QcStructureSites::of(**plan));
            }
        }
    }
    return sites;
}

std::optional<QcStructurePlacement> QcSurfaceStructures::nearest(const QString &name, QcBlockPos from, int reach) const
{
    const std::optional<QcSurfacePlanner::KindId> kind = planner().kindIndex(name);
    if (!kind) {
        return std::nullopt;
    }
    std::shared_ptr<const QcStructurePlacement> best;
    double bestDistance = static_cast<double>(reach);
    const QList<std::pair<int, int>> regions = regionsOver({from.x - reach, from.z - reach, from.x + reach, from.z + reach}, planner().kinds()[*kind].rule.spacing);
    for (const auto &[rx, rz] : regions) {
        std::shared_ptr<const QcStructurePlacement> plan = planOf(*kind, rx, rz);
        if (!plan) {
            continue;
        }
        const double distance = std::hypot(plan->origin().x + plan->sizeX() / 2.0 - from.x, plan->origin().z + plan->sizeZ() / 2.0 - from.z);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = std::move(plan);
        }
    }
    return best ? std::optional<QcStructurePlacement>(*best) : std::nullopt;
}
