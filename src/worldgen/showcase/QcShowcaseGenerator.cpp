#include "worldgen/showcase/QcShowcaseGenerator.h"

#include "worldgen/showcase/QcShowcasePlanner.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <algorithm>

QcShowcaseGenerator::QcShowcaseGenerator(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
    QcShowcasePlanner planner(*m_content);
    m_plan = planner.build();
    m_warnings = planner.warnings();
}

std::shared_ptr<QcChunk> QcShowcaseGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    for (int y = 0; y < m_plan.groundColumn.size() && y < QcWorld::worldHeight; ++y) {
        const QcBlockId block = m_plan.groundColumn[y];
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                chunk->setBlock(x, y, z, block);
            }
        }
    }

    const QList<qsizetype> placements = m_plan.placementsByChunk.value(pos);
    for (const qsizetype index : placements) {
        m_plan.placements[index].writeInto(*chunk);
    }
    return chunk;
}

std::shared_ptr<const QcVillagePlan> QcShowcaseGenerator::villageFor(QcChunkPos pos) const
{
    for (const std::shared_ptr<const QcVillagePlan> &village : m_plan.villages) {
        if (village->overlaps(pos, 0)) {
            return village;
        }
    }
    return nullptr;
}

QList<QcGuardSite> QcShowcaseGenerator::guardSitesNear(QcBlockPos from, int reach) const
{
    QList<QcGuardSite> sites;
    for (const QcGuardSite &site : m_plan.guardSites) {
        const int dx = std::max({site.min.x - from.x, 0, from.x - site.max.x});
        const int dz = std::max({site.min.z - from.z, 0, from.z - site.max.z});
        if (dx <= reach && dz <= reach) {
            sites.append(site);
        }
    }
    return sites;
}
