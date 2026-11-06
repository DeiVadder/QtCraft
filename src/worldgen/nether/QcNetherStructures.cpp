// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherStructures.h"

#include "common/core/QcIntMath.h"
#include "worldgen/fortress/QcFortressGenerator.h"
#include "worldgen/nether/QcNetherTerrain.h"
#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Tries of a place in a region.
constexpr int attempts = 8;
// The floors under the corners and the middle of a copy differ by at most this many blocks.
constexpr int maxSlope = 6;
// The deepest foundation below a copy.
constexpr int maxFoundation = 12;

} // namespace

QcNetherStructures::QcNetherStructures(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, std::shared_ptr<const QcContentRegistry> content,
                                       const QcFortressGenerator &fortresses)
    : m_definitions(definitions)
    , m_terrain(terrain)
    , m_content(std::move(content))
    , m_fortresses(fortresses)
{
}

void QcNetherStructures::setStation(QcBlockPos middle)
{
    if (!m_definitions.station) {
        return;
    }
    const QcStructure &station = *m_definitions.station;
    m_station.emplace(m_definitions.station, QcBlockPos{middle.x - station.sizeX() / 2, middle.y, middle.z - station.sizeZ() / 2}, 0, *m_content);
}

std::optional<QcStructurePlacement> QcNetherStructures::planOf(const QcNetherDefinitions::Structure &kind, int regionX, int regionZ) const
{
    if (qcPositionRandom(kind.seed, regionX, 0, regionZ) >= kind.chance) {
        return std::nullopt;
    }
    const QcStructure &structure = *kind.structure;
    const int room = structure.sizeY() + 1;
    const int size = std::max(structure.sizeX(), structure.sizeZ());
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        const int x = regionX * kind.spacing + static_cast<int>(qcPositionRandom(kind.seed, regionX, attempt * 4, regionZ) * (kind.spacing - size));
        const int z = regionZ * kind.spacing + static_cast<int>(qcPositionRandom(kind.seed, regionX, attempt * 4 + 1, regionZ) * (kind.spacing - size));
        const std::optional<int> middle = m_terrain.floorAt(x + size / 2, z + size / 2, 0, room);
        if (!middle) {
            continue;
        }
        bool level = true;
        for (const auto &[dx, dz] : {std::pair{0, 0}, {size - 1, 0}, {0, size - 1}, {size - 1, size - 1}}) {
            const std::optional<int> corner = m_terrain.floorAt(x + dx, z + dz, *middle - maxSlope, 2);
            level = level && corner && std::abs(*corner - *middle) <= maxSlope;
        }
        // A fortress never yields: it plans first, and a copy that would stand in it tries elsewhere.
        if (level && m_fortresses.plansIn({x, z, x + size - 1, z + size - 1}).isEmpty()) {
            const int turns = static_cast<int>(qcPositionRandom(kind.seed, regionX, attempt * 4 + 2, regionZ) * 4.0);
            return QcStructurePlacement(kind.structure, QcBlockPos{x, *middle + 1 - structure.groundLayer, z}, turns, *m_content);
        }
    }
    return std::nullopt;
}

QList<QcStructurePlacement> QcNetherStructures::plansIn(const QcNetherDefinitions::Structure &kind, QcXzRect area) const
{
    QList<QcStructurePlacement> plans;
    // A region's size is no multiple of a chunk's for every spacing: a chunk may touch four.
    for (int regionZ = qcFloorDiv(area.minZ, kind.spacing); regionZ <= qcFloorDiv(area.maxZ, kind.spacing); ++regionZ) {
        for (int regionX = qcFloorDiv(area.minX, kind.spacing); regionX <= qcFloorDiv(area.maxX, kind.spacing); ++regionX) {
            if (std::optional<QcStructurePlacement> plan = planOf(kind, regionX, regionZ)) {
                plans.append(std::move(*plan));
            }
        }
    }
    return plans;
}

QList<QcStructurePlacement> QcNetherStructures::plansTouching(QcChunkPos pos, int margin) const
{
    QList<QcStructurePlacement> plans;
    const int x = pos.x * QcWorld::chunkSize - margin;
    const int z = pos.z * QcWorld::chunkSize - margin;
    const int size = QcWorld::chunkSize + 2 * margin;
    const QcXzRect area{x, z, x + size - 1, z + size - 1};
    const auto touches = [&](const QcStructurePlacement &plan) {
        const QcBlockPos origin = plan.origin();
        return origin.x < x + size && origin.x + plan.sizeX() > x && origin.z < z + size && origin.z + plan.sizeZ() > z;
    };
    for (const QcNetherDefinitions::Structure &kind : m_definitions.structures) {
        const QList<QcStructurePlacement> candidates = plansIn(kind, area);
        std::ranges::copy_if(candidates, std::back_inserter(plans), touches);
    }
    if (station() && touches(*station())) {
        plans.append(*station());
    }
    return plans;
}

void QcNetherStructures::prepare(QcChunk &chunk, const QcStructurePlacement &plan) const
{
    const int groundLayer = plan.structure().groundLayer;
    const int floor = plan.origin().y + groundLayer;
    const int top = std::min(plan.origin().y + plan.structure().sizeY(), m_definitions.settings.roof - 1);
    const QcBlockPos origin = plan.origin();
    for (int z = 0; z < QcWorld::chunkSize; ++z) {
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            const int worldX = chunk.pos().x * QcWorld::chunkSize + x;
            const int worldZ = chunk.pos().z * QcWorld::chunkSize + z;
            if (worldX < origin.x || worldX >= origin.x + plan.sizeX() || worldZ < origin.z || worldZ >= origin.z + plan.sizeZ()) {
                continue;
            }
            for (int y = floor; y <= top; ++y) {
                chunk.setBlock(x, y, z, qcAirBlockId);
            }
            const int deepest = std::max(1, floor - maxFoundation);
            for (int y = floor - 1; y >= deepest && chunk.block(x, y, z) != m_definitions.filler; --y) {
                chunk.setBlock(x, y, z, m_definitions.filler);
            }
        }
    }
}

void QcNetherStructures::sealPools(QcChunk &chunk, const QcStructurePlacement &plan) const
{
    const QcBlockPos origin = plan.origin();
    const int floor = origin.y + plan.structure().groundLayer;
    const int top = origin.y + plan.structure().sizeY() + 1;
    m_definitions.sealPools(chunk, {{origin.x - 1, floor, origin.z - 1}, {origin.x + plan.sizeX(), top, origin.z + plan.sizeZ()}});
}

void QcNetherStructures::build(QcChunk &chunk) const
{
    // All pools first: a structure may hold the same liquid.
    const QList<QcStructurePlacement> near = plansTouching(chunk.pos(), 1);
    for (const QcStructurePlacement &plan : near) {
        sealPools(chunk, plan);
    }

    const QList<QcStructurePlacement> plans = plansTouching(chunk.pos());
    for (const QcStructurePlacement &plan : plans) {
        prepare(chunk, plan);
        plan.writeInto(chunk);
    }
}

QcChunkFurnishing QcNetherStructures::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing;
    const QList<QcStructurePlacement> plans = plansTouching(pos);
    for (const QcStructurePlacement &plan : plans) {
        for (const QcStructureDefinition::Loot &loot : plan.structure().loot) {
            const QcBlockPos at = plan.toWorld(loot.at);
            if (at.chunk() == pos) {
                furnishing.chests.append({at, loot.table, static_cast<quint64>(qHashMulti(0x591u, at.x, at.y, at.z))});
            }
        }
    }
    return furnishing;
}

QList<QcWorldLabel> QcNetherStructures::labelsIn(QcChunkPos pos) const
{
    QList<QcWorldLabel> labels;
    if (!station() || !station()->overlaps(pos)) {
        return labels;
    }
    for (const QcStructure::Label &label : station()->structure().labels) {
        const QcVec3 at = station()->toWorld(label.at);
        const QcBlockPos cell{static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)), static_cast<int>(std::floor(at.z))};
        if (cell.chunk() == pos) {
            labels.append({at, label.text, label.detail, label.heading});
        }
    }
    return labels;
}

QStringList QcNetherStructures::names() const
{
    QStringList names;
    for (const QcNetherDefinitions::Structure &kind : m_definitions.structures) {
        names.append(kind.name);
    }
    return names;
}

std::optional<QcStructurePlacement> QcNetherStructures::nearest(const QString &name, QcBlockPos from, int reach) const
{
    std::optional<QcStructurePlacement> best;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    const QcXzRect area{from.x - reach, from.z - reach, from.x + reach, from.z + reach};
    for (const QcNetherDefinitions::Structure &kind : m_definitions.structures) {
        if (kind.name != name && kind.structure->name() != name) {
            continue;
        }
        const QList<QcStructurePlacement> candidates = plansIn(kind, area);
        for (const QcStructurePlacement &plan : std::as_const(candidates)) {
            const qint64 dx = plan.origin().x - from.x;
            const qint64 dz = plan.origin().z - from.z;
            if (dx * dx + dz * dz < bestDistance) {
                bestDistance = dx * dx + dz * dz;
                best = plan;
            }
        }
    }
    return best;
}
