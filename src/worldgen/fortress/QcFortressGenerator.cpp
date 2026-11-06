// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressGenerator.h"

#include "common/core/QcIntMath.h"
#include "worldgen/terrain/QcNoise.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Plans kept: more regions than a game visits at once.
constexpr qsizetype cacheCapacity = 256;
// The regions one /locate plans at most, on the tick thread.
constexpr qsizetype maxLocatedRegions = 64;

// The columns of the chunk `pos`.
QcXzRect columnsOf(QcChunkPos pos)
{
    const int x = pos.x * QcWorld::chunkSize;
    const int z = pos.z * QcWorld::chunkSize;
    return {x, z, x + QcWorld::chunkSize - 1, z + QcWorld::chunkSize - 1};
}

} // namespace

QcFortressGenerator::QcFortressGenerator(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, const QcContentRegistry &content, quint64 seed)
    : m_planner(definitions, terrain, seed)
    , m_lootSeed(qcMixSeed(seed, 0xf0a7))
{
    if (!m_planner.enabled()) {
        return;
    }
    QcFortressBlocks::Resolved resolved = QcFortressBlocks::resolve(m_planner.settings(), content);
    m_warnings = resolved.warnings;
    m_blocks = std::move(resolved.blocks);
    if (m_blocks) {
        m_builder.emplace(*m_blocks, definitions, qcMixSeed(seed, 0xb71c));
    }
}

void QcFortressGenerator::setDemonstrationPlan(std::shared_ptr<const QcFortressPlan> plan)
{
    m_demonstration = std::move(plan);
}

void QcFortressGenerator::keepClearOf(QcXzRect area)
{
    m_clear.append(area);
}

const QList<QcXzRect> &QcFortressGenerator::clearAreas() const
{
    return m_clear;
}

void QcFortressGenerator::setDemonstration(QcBlockPos corner)
{
    if (hasBuilder()) {
        // The net's shape does not depend on where it stands: moved so its west edge lies at the
        // corner, whichever lattice points it took.
        const int shift = corner.x - planner().planAt(corner)->bounds.minX;
        setDemonstrationPlan(planner().planAt(corner.offset(shift, 0, 0)));
        keepClearOf(demonstration()->bounds);
    }
}

qint64 QcFortressGenerator::keyOf(int regionX, int regionZ)
{
    return (static_cast<qint64>(regionX) << 32) ^ static_cast<qint64>(static_cast<quint32>(regionZ));
}

bool QcFortressGenerator::isBuilt(const QcFortressPlan &plan) const
{
    return std::ranges::none_of(clearAreas(), [&plan](QcXzRect area) { return area.intersects(plan.bounds); });
}

std::shared_ptr<const QcFortressPlan> QcFortressGenerator::planOf(int regionX, int regionZ) const
{
    if (!hasBuilder()) {
        return nullptr;
    }
    const qint64 key = keyOf(regionX, regionZ);
    {
        const std::scoped_lock lock(m_mutex);
        if (const std::optional<std::shared_ptr<const QcFortressPlan>> cached = cachedPlan(key)) {
            return *cached;
        }
    }
    std::shared_ptr<const QcFortressPlan> plan = planner().plan(regionX, regionZ);
    if (plan && !isBuilt(*plan)) {
        plan = nullptr;
    }
    const std::scoped_lock lock(m_mutex);
    return cachePlan(key, std::move(plan));
}

std::optional<std::shared_ptr<const QcFortressPlan>> QcFortressGenerator::cachedPlan(qint64 key) const
{
    const auto it = plans().constFind(key);
    return it == plans().cend() ? std::nullopt : std::optional(*it);
}

std::shared_ptr<const QcFortressPlan> QcFortressGenerator::cachePlan(qint64 key, std::shared_ptr<const QcFortressPlan> plan) const
{
    if (plans().size() >= cacheCapacity) {
        plans().clear();
    }
    return *plans().insert(key, std::move(plan));
}

template <typename Visit>
void QcFortressGenerator::forRegions(QcXzRect area, Visit visit) const
{
    if (!hasBuilder()) {
        return;
    }
    const int size = planner().settings().regionSize;
    for (int regionZ = qcFloorDiv(area.minZ, size); regionZ <= qcFloorDiv(area.maxZ, size); ++regionZ) {
        for (int regionX = qcFloorDiv(area.minX, size); regionX <= qcFloorDiv(area.maxX, size); ++regionX) {
            visit(regionX, regionZ);
        }
    }
}

QList<std::shared_ptr<const QcFortressPlan>> QcFortressGenerator::plansIn(QcXzRect area) const
{
    QList<std::shared_ptr<const QcFortressPlan>> plans;
    forRegions(area, [&](int regionX, int regionZ) {
        std::shared_ptr<const QcFortressPlan> plan = planOf(regionX, regionZ);
        if (plan && plan->touches(area)) {
            plans.append(std::move(plan));
        }
    });
    if (demonstration() && demonstration()->touches(area)) {
        plans.append(demonstration());
    }
    return plans;
}

void QcFortressGenerator::build(QcChunk &chunk) const
{
    if (!hasBuilder()) {
        return;
    }
    const QList<std::shared_ptr<const QcFortressPlan>> plans = plansIn(columnsOf(chunk.pos()));
    for (const std::shared_ptr<const QcFortressPlan> &plan : plans) {
        builder().build(chunk, *plan);
    }
}

QcChunkFurnishing QcFortressGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    const QcNetherSettings::Fortress &settings = planner().settings();
    const QList<std::shared_ptr<const QcFortressPlan>> plans = plansIn(columnsOf(pos));
    for (const std::shared_ptr<const QcFortressPlan> &plan : plans) {
        for (const QcFortressPlan::Chest &chest : plan->chests) {
            if (chest.at.chunk() == pos) {
                const auto draw = static_cast<quint64>(qcPositionRandom(m_lootSeed, chest.at.x, chest.at.y, chest.at.z) * 4294967296.0);
                furnishing.chests.append({chest.at, settings.lootTable, qcMixSeed(m_lootSeed, draw)});
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
    // The boxes of the parts as in the original game's Java edition; the box around them all,
    // which the boxes leave mostly empty, is only the area's outline.
    QcSpawnArea area{{plan.bounds.minX, QcWorld::worldHeight - 1, plan.bounds.minZ}, {plan.bounds.maxX, 0, plan.bounds.maxZ}, planner().settings().monsters, plan.parts};
    for (const QcBlockBox &part : plan.parts) {
        area.min.y = std::min(area.min.y, part.low.y);
        area.max.y = std::max(area.max.y, part.high.y);
    }
    return area;
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
    const QcXzRect area{from.x - reach, from.z - reach, from.x + reach, from.z + reach};
    QcStructureSites sites;
    bool planned = true;
    {
        const std::scoped_lock lock(m_mutex);
        forRegions(area, [&](int regionX, int regionZ) {
            const std::optional<std::shared_ptr<const QcFortressPlan>> cached = cachedPlan(keyOf(regionX, regionZ));
            if (!cached) {
                planned = false;
            } else if (*cached && (*cached)->touches(area)) {
                sites.spawnAreas.append(areaOf(**cached));
            }
        });
    }
    if (demonstration() && demonstration()->touches(area)) {
        sites.spawnAreas.append(areaOf(*demonstration()));
    }
    return planned ? std::optional(sites) : std::nullopt;
}

QList<QcWorldLabel> QcFortressGenerator::labelsIn(QcChunkPos pos) const
{
    QList<QcWorldLabel> labels;
    if (!demonstration()) {
        return labels;
    }
    const QcFortressPlan &plan = *demonstration();
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
    if (!hasBuilder()) {
        return {std::nullopt, reach};
    }
    const auto placeOf = [this](int regionX, int regionZ) -> std::optional<QcLocatedPlace> {
        const std::shared_ptr<const QcFortressPlan> plan = planOf(regionX, regionZ);
        return plan ? std::optional(QcLocatedPlace{plan->middle(), std::nullopt}) : std::nullopt;
    };
    QcLocateResult result = QcPlaceSearch::nearestInRegions(from, reach, planner().settings().regionSize, maxLocatedRegions, placeOf);
    if (demonstration()) {
        const QcBlockPos middle = demonstration()->middle();
        if (!result.place || QcPlaceSearch::distance(middle, from) < QcPlaceSearch::distance(result.place->at, from)) {
            result.place = QcLocatedPlace{middle, std::nullopt};
        }
    }
    return result;
}
