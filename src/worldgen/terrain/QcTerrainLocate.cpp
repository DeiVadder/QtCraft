// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// What /locate finds of the terrain generator (decision E-911): villages, mine shafts, dungeons,
// coastal towers, strongholds (decision E-1173) and the surface structures; and where they reach
// (decision E-1013).

#include "worldgen/dungeons/QcDungeonGenerator.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/strongholds/QcStrongholdGenerator.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/towers/QcCoastalTowerGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

using namespace Qt::StringLiterals;

namespace {

// How many regions one search may plan at most (decision E-911), so a miss does not stall the tick:
// a mine shaft or coastal tower region takes about 30 ms to plan in a debug build (64 regions reach
// about 850 and 1700 blocks), a dungeon region about 2.5 ms (512 regions reach about 600 blocks and
// stay well within the dungeon plan cache).
constexpr qsizetype mineshaftBudget = 64;
constexpr qsizetype dungeonBudget = 512;
constexpr qsizetype coastalTowerBudget = 64;

} // namespace

QStringList QcTerrainGenerator::locatableNames() const
{
    QStringList names{u"village"_s};
    if (mineshafts().planner().enabled()) {
        names.append(u"mineshaft"_s);
    }
    if (dungeons().planner().enabled()) {
        names.append(u"dungeon"_s);
    }
    if (coastalTowers().planner().enabled()) {
        names.append(u"coastal_tower"_s);
    }
    if (hasStrongholds()) {
        names.append(u"stronghold"_s);
    }
    for (const QcSurfacePlanner::Kind &kind : surfaceStructures().planner().kinds()) {
        names.append(kind.name.startsWith(u"qtcraft:"_s) ? kind.name.mid(8) : kind.name);
    }
    names.removeDuplicates();
    return names;
}

QcLocateResult QcTerrainGenerator::locate(QStringView kind, QcBlockPos from, int reach) const
{
    if (kind == u"mineshaft"_s && mineshafts().planner().enabled()) {
        return QcPlaceSearch::nearestInRegions(from, reach, mineshafts().planner().settings().regionSize, mineshaftBudget,
                                               [this](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
                                                   const std::shared_ptr<const QcMineshaftPlan> plan = mineshafts().builtPlanOf(regionX, regionZ);
                                                   if (!plan) {
                                                       return std::nullopt;
                                                   }
                                                   const std::optional<QcBlockPos> entrance = plan->entrance ? std::optional(plan->entrance->out)
                                                                                                             : std::nullopt;
                                                   return QcLocatedPlace{plan->nodes.constFirst().centre, entrance};
                                               });
    }
    if (kind == u"dungeon"_s && dungeons().planner().enabled()) {
        return QcPlaceSearch::nearestInRegions(from, reach, dungeons().planner().settings().regionSize, dungeonBudget,
                                               [this](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
                                                   const std::shared_ptr<const QcDungeonPlan> plan = dungeons().builtPlanOf(regionX, regionZ);
                                                   if (!plan) {
                                                       return std::nullopt;
                                                   }
                                                   return QcLocatedPlace{plan->origin.offset(plan->sizeX / 2, 1, plan->sizeZ / 2), std::nullopt};
                                               });
    }
    if (kind == u"coastal_tower"_s && coastalTowers().planner().enabled()) {
        return QcPlaceSearch::nearestInRegions(from, reach, coastalTowers().planner().settings().regionSize, coastalTowerBudget,
                                               [this](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
                                                   const std::shared_ptr<const QcCoastalTowerPlan> plan = coastalTowers().builtPlanOf(regionX, regionZ);
                                                   return plan ? std::optional(QcLocatedPlace{plan->doorstep, std::nullopt}) : std::nullopt;
                                               });
    }
    if (kind == u"stronghold"_s && hasStrongholds()) {
        return strongholds().nearest(from, reach);
    }
    return {std::nullopt, reach};
}

bool QcTerrainGenerator::hasStrongholds() const
{
    return version() >= QcStrongholdGenerator::firstVersion && strongholds().enabled();
}

std::optional<QString> QcTerrainGenerator::structureAt(QcBlockPos at) const
{
    if (!hasStrongholds()) {
        return QString();
    }
    const std::optional<bool> inStronghold = strongholds().plannedHolds(at);
    if (!inStronghold) {
        return std::nullopt;
    }
    return *inStronghold ? u"stronghold"_s : QString();
}

QList<QcXzRect> QcTerrainGenerator::structuresTouching(QcChunkPos pos) const
{
    QList<QcXzRect> reaches = surfaceStructures().reachesTouching(pos);
    const QList<std::shared_ptr<const QcDungeonPlan>> dungeonPlans = dungeons().builtPlansTouching(pos);
    for (const std::shared_ptr<const QcDungeonPlan> &plan : dungeonPlans) {
        reaches.append(plan->bounds());
    }
    const QList<std::shared_ptr<const QcMineshaftPlan>> shafts = mineshafts().builtPlansTouching(pos);
    for (const std::shared_ptr<const QcMineshaftPlan> &plan : shafts) {
        reaches.append(plan->bounds());
    }
    const QList<std::shared_ptr<const QcCoastalTowerPlan>> towers = coastalTowers().builtPlansTouching(pos);
    for (const std::shared_ptr<const QcCoastalTowerPlan> &plan : towers) {
        reaches.append(QcCoastalTowerGenerator::reachOf(*plan));
    }
    if (hasStrongholds()) {
        const QList<std::shared_ptr<const QcStrongholdPlan>> strongholdPlans = strongholds().plansIn(QcXzRect::ofChunk(pos));
        for (const std::shared_ptr<const QcStrongholdPlan> &plan : strongholdPlans) {
            reaches.append(plan->bounds);
        }
    }
    if (const std::shared_ptr<const QcVillagePlan> village = villages().builtPlanFor(pos); village && village->overlaps(pos, 0)) {
        reaches.append(village->bounds());
    }
    return reaches;
}
