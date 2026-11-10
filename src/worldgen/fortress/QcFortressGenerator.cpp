// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressGenerator.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The regions one /locate plans at most, on the tick thread.
constexpr qsizetype maxLocatedRegions = 64;

} // namespace

QcFortressGenerator::QcFortressGenerator(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcNetherComplexes &complexes,
                                         const QcContentRegistry &content, quint64 seed)
    : m_planner(definitions, terrain, complexes, seed)
    , m_plans(0, {})
    , m_lootSeed(qcMixSeed(seed, 0xf0a7))
{
    if (!planner().enabled()) {
        return;
    }
    QcFortressBlocks::Resolved resolved = QcFortressBlocks::resolve(planner().settings(), content);
    m_warnings = resolved.warnings;
    m_blocks = std::move(resolved.blocks);
    if (m_blocks) {
        m_builder.emplace(*m_blocks, definitions, qcMixSeed(seed, 0xb71c));
        m_plans.setPlanner(planner().settings().regionSize, [this](int regionX, int regionZ) { return planner().plan(regionX, regionZ); });
    }
}

void QcFortressGenerator::keepClearOf(QcXzRect area)
{
    m_plans.keepClearOf(area);
}

std::shared_ptr<const QcFortressPlan> QcFortressGenerator::demonstration() const
{
    return plans().demonstrations().isEmpty() ? nullptr : plans().demonstrations().constFirst();
}

void QcFortressGenerator::setDemonstration(QcBlockPos corner)
{
    if (hasBuilder()) {
        // The net's shape does not depend on where it stands: moved so its west edge lies at the
        // corner, whichever lattice points it took.
        const int shift = corner.x - planner().planAt(corner)->bounds.minX;
        m_plans.addDemonstration(planner().planAt(corner.offset(shift, 0, 0)));
        keepClearOf(demonstration()->bounds);
    }
}

void QcFortressGenerator::build(QcChunk &chunk) const
{
    if (!hasBuilder()) {
        return;
    }
    const QList<std::shared_ptr<const QcFortressPlan>> plans = plansIn(QcXzRect::ofChunk(chunk.pos()));
    for (const std::shared_ptr<const QcFortressPlan> &plan : plans) {
        builder().build(chunk, *plan);
    }
}

QcChunkFurnishing QcFortressGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    const QcNetherSettings::Fortress &settings = planner().settings();
    const QList<std::shared_ptr<const QcFortressPlan>> plans = plansIn(QcXzRect::ofChunk(pos));
    for (const std::shared_ptr<const QcFortressPlan> &plan : plans) {
        for (const QcFortressPlan::Chest &chest : plan->chests) {
            if (chest.at.chunk() == pos) {
                furnishing.chests.append({chest.at, settings.lootTable, QcChunkFurnishing::chestSeed(m_lootSeed, chest.at)});
            }
        }
        for (const QcBlockPos &spawner : plan->spawners) {
            if (spawner.chunk() == pos) {
                furnishing.spawners.append({spawner, settings.spawnerMob});
            }
        }
    }
    return furnishing;
}

QcSpawnArea QcFortressGenerator::areaOf(const QcFortressPlan &plan) const
{
    // The boxes of the parts as in the original game's Java edition.
    return QcSpawnArea::ofParts(plan.bounds, planner().settings().monsters, plan.parts);
}

QcStructureSites QcFortressGenerator::sitesNear(QcBlockPos from, int reach) const
{
    QcStructureSites sites;
    const QList<std::shared_ptr<const QcFortressPlan>> plans = plansIn({from.x - reach, from.z - reach, from.x + reach, from.z + reach});
    for (const std::shared_ptr<const QcFortressPlan> &plan : plans) {
        sites.spawnAreas.append(areaOf(*plan));
    }
    return sites;
}

std::optional<QcStructureSites> QcFortressGenerator::plannedSitesNear(QcBlockPos from, int reach) const
{
    const std::optional<QList<std::shared_ptr<const QcFortressPlan>>> planned = plans().plannedIn({from.x - reach, from.z - reach, from.x + reach, from.z + reach});
    if (!planned) {
        return std::nullopt;
    }
    QcStructureSites sites;
    for (const std::shared_ptr<const QcFortressPlan> &plan : *planned) {
        sites.spawnAreas.append(areaOf(*plan));
    }
    return sites;
}

std::optional<bool> QcFortressGenerator::plannedHolds(QcBlockPos at) const
{
    const std::optional<QList<std::shared_ptr<const QcFortressPlan>>> planned = plans().plannedIn({at.x, at.z, at.x, at.z});
    if (!planned) {
        return std::nullopt;
    }
    return std::ranges::any_of(*planned, [&](const std::shared_ptr<const QcFortressPlan> &plan) { return areaOf(*plan).contains(at); });
}

QList<QcWorldLabel> QcFortressGenerator::labelsIn(QcChunkPos pos) const
{
    QList<QcWorldLabel> labels;
    const std::shared_ptr<const QcFortressPlan> shown = demonstration();
    if (!shown) {
        return labels;
    }
    const QcFortressPlan &plan = *shown;
    const auto add = [&](QcBlockPos cell, double height, const QString &text, bool heading) {
        if (cell.chunk() == pos) {
            labels.append({{cell.x + 0.5, cell.y + height, cell.z + 0.5}, text, {}, heading});
        }
    };
    add(plan.rooms.constFirst().centre, 3.6, u"structure.qtcraft.fortress"_s, true);
    for (const QcBlockPos &spawner : plan.spawners) {
        add(spawner, 1.6, u"block.qtcraft.spawner"_s, false);
    }
    for (const QcFortressPlan::Room &room : plan.rooms) {
        if (room.kind == QcFortressPlan::RoomKind::Garden) {
            add(room.centre, 3.6, u"block.qtcraft.nether_wart_crop"_s, false);
        }
    }
    return labels;
}

QcLocateResult QcFortressGenerator::nearest(QcBlockPos from, int reach) const
{
    return plans().nearest(from, reach, maxLocatedRegions);
}
