// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawning/QcSpawnAreas.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerWorld.h"

#include <algorithm>
#include <utility>

QcSpawnAreas::QcSpawnAreas(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

QcMobDefinition::SpawnRule QcSpawnAreas::ruleOf(const QcMobDefinition &definition)
{
    // The kind's light test and ground stay (darkness, block light, blocks); the place is the area.
    QcMobDefinition::SpawnRule rule = definition.spawnRules().constFirst();
    rule.time = QcMobDefinition::SpawnTime::Any;
    rule.weight = 1.0;
    rule.caves = true;
    rule.chunkChance = 1.0;
    rule.maxY = QcWorld::worldHeight;
    rule.biomes.clear();
    rule.sky = false;
    rule.water = false;
    return rule;
}

void QcSpawnAreas::refresh(QcServerWorld &world, const QList<QcMobExchange::PlayerView> &players)
{
    // The first call and every refreshTicks-th after it.
    const bool due = m_ticks % refreshTicks == 0;
    m_ticks += 1;
    if (!due) {
        return;
    }

    // Only planned regions: planning one takes milliseconds, it happens on a generation thread and
    // the areas there come with a later refresh. Until then a player keeps the areas it had, so
    // other monsters do not spawn in a hut meanwhile.
    QList<QcSpawnArea> sites;
    for (const QcMobExchange::PlayerView &player : players) {
        if (!player.alive) {
            continue;
        }
        const QcBlockPos from = QcPhysics::blockContaining(player.position);
        const std::optional<QcStructureSites> near = world.generator().plannedStructureSitesNear(from, reach);
        if (near) {
            sites.append(near->spawnAreas);
        } else {
            world.planStructureSitesInBackground(from, reach);
            sites.append(keptNear(from));
        }
    }
    m_areas = resolveAll(sites);
}

QList<QcSpawnArea> QcSpawnAreas::keptNear(QcBlockPos from) const
{
    QList<QcSpawnArea> kept;
    for (const Area &area : m_areas) {
        const int dx = std::max({area.site.min.x - from.x, 0, from.x - area.site.max.x});
        const int dz = std::max({area.site.min.z - from.z, 0, from.z - area.site.max.z});
        if (dx <= reach && dz <= reach) {
            kept.append(area.site);
        }
    }
    return kept;
}

QList<QcSpawnAreas::Area> QcSpawnAreas::resolveAll(const QList<QcSpawnArea> &sites) const
{
    // Players close together see the same areas: each once.
    QList<Area> areas;
    for (const QcSpawnArea &site : sites) {
        const bool known = std::ranges::any_of(areas, [&site](const Area &area) { return area.site.min == site.min; });
        if (!known) {
            areas.append(resolve(site));
        }
    }
    return areas;
}

const QcSpawnAreas::Area *QcSpawnAreas::at(int x, int z) const
{
    for (const Area &area : m_areas) {
        if (area.site.containsColumn(x, z)) {
            return &area;
        }
    }
    return nullptr;
}

QcSpawnAreas::Area QcSpawnAreas::resolve(const QcSpawnArea &site) const
{
    Area area{site, {}};
    for (const QString &name : site.monsters) {
        // Unknown kinds and kinds that are no monsters were reported when the content was loaded.
        const std::optional<int> type = m_content->mobIndex(name);
        if (type && m_content->mobs()[*type].isMonster()) {
            area.types.append(*type);
        }
    }
    return area;
}
