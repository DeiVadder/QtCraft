// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// What /locate finds of the terrain generator (decision E-911): villages, mine shafts, dungeons,
// coastal towers and the surface structures.

#include "worldgen/dungeons/QcDungeonGenerator.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/towers/QcCoastalTowerGenerator.h"

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
                                                   const std::shared_ptr<const QcMineshaftPlan> plan = mineshafts().planOf(regionX, regionZ);
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
                                                   const std::shared_ptr<const QcDungeonPlan> plan = dungeons().planOf(regionX, regionZ);
                                                   if (!plan) {
                                                       return std::nullopt;
                                                   }
                                                   return QcLocatedPlace{plan->origin.offset(plan->sizeX / 2, 1, plan->sizeZ / 2), std::nullopt};
                                               });
    }
    if (kind == u"coastal_tower"_s && coastalTowers().planner().enabled()) {
        return QcPlaceSearch::nearestInRegions(from, reach, coastalTowers().planner().settings().regionSize, coastalTowerBudget,
                                               [this](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
                                                   const std::shared_ptr<const QcCoastalTowerPlan> plan = coastalTowers().planOf(regionX, regionZ);
                                                   return plan ? std::optional(QcLocatedPlace{plan->doorstep, std::nullopt}) : std::nullopt;
                                               });
    }
    return {std::nullopt, reach};
}
