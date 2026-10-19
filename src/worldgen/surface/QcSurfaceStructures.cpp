#include "worldgen/surface/QcSurfaceStructures.h"

#include "worldgen/villages/QcVillageGenerator.h"

#include <cmath>

namespace {

int floorDiv(int value, int divisor)
{
    return int(std::floor(double(value) / divisor));
}

// The regions of size `spacing` that the columns x0..x1, z0..z1 reach into.
QList<std::pair<int, int>> regionsOver(QcSurfacePlanner::Rect columns, int spacing)
{
    QList<std::pair<int, int>> regions;
    for (int rz = floorDiv(columns.z0, spacing); rz <= floorDiv(columns.z1, spacing); ++rz) {
        for (int rx = floorDiv(columns.x0, spacing); rx <= floorDiv(columns.x1, spacing); ++rx) {
            regions.append({rx, rz});
        }
    }
    return regions;
}

QcSurfacePlanner::Rect grown(QcSurfacePlanner::Rect rect, int margin)
{
    return {rect.x0 - margin, rect.z0 - margin, rect.x1 + margin, rect.z1 + margin};
}

// The cache key of a kind's region: 24 bits per region coordinate (regions are at least 64 blocks).
quint64 planKey(int kind, int regionX, int regionZ)
{
    return (quint64(kind) << 48) | ((quint64(quint32(regionX)) & 0xffffffU) << 24) | (quint64(quint32(regionZ)) & 0xffffffU);
}

} // namespace

QcSurfaceStructures::QcSurfaceStructures(const QcTerrainGenerator &terrain, const QcVillageGenerator &villages, const QcContentRegistry &content,
                                         quint64 seed, QStringList *warnings)
    : m_villages(villages)
    , m_planner(terrain, content, seed, warnings)
{
    m_ground.resize(std::size_t(content.stateCount()));
    for (std::size_t id = 0; id < m_ground.size(); ++id) {
        const QcBlockDefinition &block = content.block(QcBlockId(id));
        m_ground[id] = block.solid && !block.liquid;
    }
}

std::shared_ptr<const QcStructurePlacement> QcSurfaceStructures::planOf(int kind, int regionX, int regionZ) const
{
    const quint64 key = planKey(kind, regionX, regionZ);
    {
        const QMutexLocker lock(&m_mutex);
        if (const auto it = m_plans.constFind(key); it != m_plans.cend()) {
            return *it;
        }
    }
    // Planned outside the lock: planning asks the villages and the kinds before this one.
    const auto occupied = [this](int planned, QcSurfacePlanner::Rect footprint, int margin) { return this->occupied(planned, footprint, margin); };
    std::optional<QcStructurePlacement> placement = m_planner.plan(kind, regionX, regionZ, occupied);
    const std::shared_ptr<const QcStructurePlacement> plan =
        placement ? std::make_shared<const QcStructurePlacement>(std::move(*placement)) : nullptr;
    const QMutexLocker lock(&m_mutex);
    if (m_plans.size() >= cacheCapacity) {
        m_plans.clear();
    }
    return *m_plans.insert(key, plan);
}

std::optional<std::shared_ptr<const QcStructurePlacement>> QcSurfaceStructures::plannedOf(int kind, int regionX, int regionZ) const
{
    const QMutexLocker lock(&m_mutex);
    const auto it = m_plans.constFind(planKey(kind, regionX, regionZ));
    return it == m_plans.cend() ? std::nullopt : std::optional(*it);
}

bool QcSurfaceStructures::occupied(int kind, QcSurfacePlanner::Rect footprint, int margin) const
{
    // Villages: every region of their spacing the footprint and its margin reach into.
    const QcSurfacePlanner::Rect reach = grown(footprint, margin);
    const QList<std::pair<int, int>> villageRegions = regionsOver(reach, m_villages.spacing());
    for (const auto &[rx, rz] : villageRegions) {
        const std::shared_ptr<const QcVillagePlan> village = m_villages.regionPlan(rx, rz);
        if (village && footprint.intersects({village->minX, village->minZ, village->maxX, village->maxZ}, margin)) {
            return true;
        }
    }

    // The kinds before this one, each in the regions of its own spacing.
    for (int other = 0; other < kind; ++other) {
        const QList<std::pair<int, int>> regions = regionsOver(reach, m_planner.kinds()[other].rule.spacing);
        for (const auto &[rx, rz] : regions) {
            const std::shared_ptr<const QcStructurePlacement> plan = planOf(other, rx, rz);
            if (plan && footprint.intersects(QcSurfacePlanner::footprintOf(*plan), margin)) {
                return true;
            }
        }
    }
    return false;
}

QList<std::shared_ptr<const QcStructurePlacement>> QcSurfaceStructures::plansTouching(QcChunkPos pos) const
{
    // A copy lies wholly in its region and a region is whole chunks: one region per kind.
    QList<std::shared_ptr<const QcStructurePlacement>> plans;
    const int x = pos.x * QcWorld::chunkSize;
    const int z = pos.z * QcWorld::chunkSize;
    for (qsizetype kind = 0; kind < m_planner.kinds().size(); ++kind) {
        const int spacing = m_planner.kinds()[kind].rule.spacing;
        std::shared_ptr<const QcStructurePlacement> plan = planOf(int(kind), floorDiv(x, spacing), floorDiv(z, spacing));
        if (plan && plan->overlaps(pos)) {
            plans.append(std::move(plan));
        }
    }
    return plans;
}

void QcSurfaceStructures::build(QcChunk &chunk) const
{
    if (m_planner.kinds().isEmpty()) {
        return;
    }
    const QList<std::shared_ptr<const QcStructurePlacement>> plans = plansTouching(chunk.pos());
    for (const std::shared_ptr<const QcStructurePlacement> &plan : plans) {
        const std::optional<int> kind = m_planner.kindIndex(plan->structure().name());
        build(chunk, *plan, kind ? m_planner.kinds()[*kind].foundation : qcAirBlockId);
    }
}

void QcSurfaceStructures::build(QcChunk &chunk, const QcStructurePlacement &placement, QcBlockId foundation) const
{
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
            for (int y = origin.y + 1; y <= top; ++y) {
                chunk.setBlock(lx, y, lz, qcAirBlockId);
            }
            for (int y = origin.y - 1; foundation != qcAirBlockId && y > 0 && y >= origin.y - maxFoundation && !m_ground[chunk.block(lx, y, lz)]; --y) {
                chunk.setBlock(lx, y, lz, foundation);
            }
        }
    }
    placement.writeInto(chunk);
}

void QcSurfaceStructures::addFurnishing(QcChunkPos pos, QcChunkFurnishing &furnishing) const
{
    if (m_planner.kinds().isEmpty()) {
        return;
    }
    const QList<std::shared_ptr<const QcStructurePlacement>> plans = plansTouching(pos);
    for (const std::shared_ptr<const QcStructurePlacement> &plan : plans) {
        for (const QcStructureDefinition::Loot &loot : plan->structure().loot) {
            const QcBlockPos at = plan->toWorld(loot.at);
            if (at.chunk() == pos) {
                furnishing.chests.append({at, loot.table, quint64(qHashMulti(0x590u, at.x, at.y, at.z))});
            }
        }
    }
}

QList<QcGuardSite> QcSurfaceStructures::sitesNear(QcBlockPos from, int reach) const
{
    return collectSites(from, reach, true).value_or(QList<QcGuardSite>{});
}

std::optional<QList<QcGuardSite>> QcSurfaceStructures::plannedSitesNear(QcBlockPos from, int reach) const
{
    return collectSites(from, reach, false);
}

std::optional<QList<QcGuardSite>> QcSurfaceStructures::collectSites(QcBlockPos from, int reach, bool mayPlan) const
{
    QList<QcGuardSite> sites;
    const QcSurfacePlanner::Rect area{from.x - reach, from.z - reach, from.x + reach, from.z + reach};
    for (qsizetype kind = 0; kind < m_planner.kinds().size(); ++kind) {
        if (m_planner.kinds()[kind].structure->guardians.isEmpty()) {
            continue;
        }
        const QList<std::pair<int, int>> regions = regionsOver(area, m_planner.kinds()[kind].rule.spacing);
        for (const auto &[rx, rz] : regions) {
            const std::optional<std::shared_ptr<const QcStructurePlacement>> plan = mayPlan ? planOf(int(kind), rx, rz) : plannedOf(int(kind), rx, rz);
            if (!plan) {
                return std::nullopt;
            }
            if (*plan) {
                sites.append(QcGuardSite::of(**plan));
            }
        }
    }
    return sites;
}

std::optional<QcStructurePlacement> QcSurfaceStructures::nearest(const QString &name, QcBlockPos from, int reach) const
{
    const std::optional<int> kind = m_planner.kindIndex(name);
    if (!kind) {
        return std::nullopt;
    }
    std::shared_ptr<const QcStructurePlacement> best;
    double bestDistance = double(reach);
    const QList<std::pair<int, int>> regions = regionsOver({from.x - reach, from.z - reach, from.x + reach, from.z + reach}, m_planner.kinds()[*kind].rule.spacing);
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
