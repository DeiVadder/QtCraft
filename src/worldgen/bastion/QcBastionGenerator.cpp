// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/bastion/QcBastionGenerator.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>

namespace {

using Plan = QcBastionPlan;
using PlanPointer = std::shared_ptr<const QcBastionPlan>;

// The regions one /locate plans at most, on the tick thread.
constexpr qsizetype maxLocatedRegions = 64;
// The blocks between the middles of a demonstration's forms.
constexpr int demonstrationPitch = 2 * QcBastionPlan::reach + 8;

} // namespace

QcBastionGenerator::QcBastionGenerator(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcNetherComplexes &complexes,
                                       const QcContentRegistry &content, quint64 seed)
    : m_planner(definitions, terrain, complexes, seed)
    , m_plans(0, {})
    , m_lootSeed(qcMixSeed(seed, 0xba10))
{
    if (!planner().enabled()) {
        return;
    }
    QcBastionBlocks::Resolved resolved = QcBastionBlocks::resolve(planner().settings(), content);
    m_warnings = resolved.warnings;
    m_blocks = std::move(resolved.blocks);
    if (m_blocks) {
        m_builder.emplace(*m_blocks, definitions, qcMixSeed(seed, 0xba1d));
        m_plans.setPlanner(planner().settings().regionSize, [this](int regionX, int regionZ) { return planner().plan(regionX, regionZ); });
    }
}

void QcBastionGenerator::keepClearOf(QcXzRect area)
{
    m_plans.keepClearOf(area);
}

void QcBastionGenerator::setDemonstration(QcBlockPos middle)
{
    if (!hasBuilder()) {
        return;
    }
    for (int form = 0; form < Plan::formCount; ++form) {
        const int z = (2 * form - Plan::formCount + 1) * demonstrationPitch / 2;
        PlanPointer plan = planner().planAt(static_cast<Plan::Form>(form), {middle.x, planner().settings().floor, middle.z + z});
        keepClearOf(plan->bounds);
        m_plans.addDemonstration(std::move(plan));
    }
}

void QcBastionGenerator::build(QcChunk &chunk) const
{
    if (!hasBuilder()) {
        return;
    }
    const QList<PlanPointer> plans = plansIn(QcXzRect::ofChunk(chunk.pos()));
    for (const PlanPointer &plan : plans) {
        builder().build(chunk, *plan);
    }
}

const QString &QcBastionGenerator::tableOf(Plan::Loot loot) const
{
    const QcNetherSettings::Bastion &settings = planner().settings();
    switch (loot) {
    case Plan::Loot::Bridge:
        return settings.bridgeLoot;
    case Plan::Loot::Stables:
        return settings.stablesLoot;
    case Plan::Loot::Treasure:
        return settings.treasureLoot;
    case Plan::Loot::Other:
        break;
    }
    return settings.otherLoot;
}

const QString &QcBastionGenerator::nameOf(Plan::Folk folk) const
{
    const QcNetherSettings::Bastion &settings = planner().settings();
    switch (folk) {
    case Plan::Folk::Bruiser:
        return settings.bruiser;
    case Plan::Folk::Beast:
        return settings.beast;
    case Plan::Folk::Guard:
        break;
    }
    return settings.guard;
}

QcChunkFurnishing QcBastionGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    const QList<PlanPointer> plans = plansIn(QcXzRect::ofChunk(pos));
    for (const PlanPointer &plan : plans) {
        for (const Plan::Chest &chest : plan->chests) {
            if (chest.at.chunk() == pos) {
                furnishing.chests.append({chest.at, tableOf(chest.loot), QcChunkFurnishing::chestSeed(m_lootSeed, chest.at)});
            }
        }
        for (const QcBlockPos &spawner : plan->spawners) {
            if (spawner.chunk() == pos) {
                furnishing.spawners.append({spawner, planner().settings().spawnerMob});
            }
        }
        for (const Plan::Resident &resident : plan->residents) {
            if (resident.at.chunk() == pos) {
                furnishing.residents.append({resident.at, nameOf(resident.folk)});
            }
        }
    }
    return furnishing;
}

bool QcBastionGenerator::hasResidentsIn(QcChunkPos pos) const
{
    const QList<PlanPointer> plans = plansIn(QcXzRect::ofChunk(pos));
    return std::ranges::any_of(plans, [pos](const PlanPointer &plan) {
        return std::ranges::any_of(plan->residents, [pos](Plan::Resident resident) { return resident.at.chunk() == pos; });
    });
}

QcStructureSites QcBastionGenerator::sitesOf(const QList<PlanPointer> &plans) const
{
    QcStructureSites sites;
    if (!planner().settings().monsters.isEmpty()) {
        for (const PlanPointer &plan : plans) {
            sites.spawnAreas.append(QcSpawnArea::ofParts(plan->bounds, planner().settings().monsters, plan->parts));
        }
    }
    return sites;
}

QcStructureSites QcBastionGenerator::sitesNear(QcBlockPos from, int reach) const
{
    return sitesOf(plansIn({from.x - reach, from.z - reach, from.x + reach, from.z + reach}));
}

std::optional<QcStructureSites> QcBastionGenerator::plannedSitesNear(QcBlockPos from, int reach) const
{
    const std::optional<QList<PlanPointer>> planned = plans().plannedIn({from.x - reach, from.z - reach, from.x + reach, from.z + reach});
    return planned ? std::optional(sitesOf(*planned)) : std::nullopt;
}

std::optional<bool> QcBastionGenerator::plannedHolds(QcBlockPos at) const
{
    const std::optional<QList<PlanPointer>> planned = plans().plannedIn({at.x, at.z, at.x, at.z});
    if (!planned) {
        return std::nullopt;
    }
    // The part boxes, as its spawn area has them (sitesOf()).
    return std::ranges::any_of(*planned, [at](const PlanPointer &plan) { return QcSpawnArea::ofParts(plan->bounds, {}, plan->parts).contains(at); });
}

QList<QcWorldLabel> QcBastionGenerator::labelsIn(QcChunkPos pos) const
{
    QList<QcWorldLabel> labels;
    for (const PlanPointer &plan : demonstrations()) {
        for (const Plan::Sign &sign : plan->signs) {
            if (sign.at.chunk() == pos) {
                labels.append({{sign.at.x + 0.5, sign.at.y + 0.5, sign.at.z + 0.5}, sign.text, {}, sign.heading});
            }
        }
    }
    return labels;
}

QcLocateResult QcBastionGenerator::nearest(QcBlockPos from, int reach) const
{
    return plans().nearest(from, reach, maxLocatedRegions);
}
