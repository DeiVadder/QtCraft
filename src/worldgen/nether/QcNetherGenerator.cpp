// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherGenerator.h"

#include <algorithm>
#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The rings of columns around the origin the search for a spawn looks at, and their spacing.
constexpr int spawnRings = 32;
constexpr int spawnStep = 16;
// Free cells over a spawn.
constexpr int spawnRoom = 3;
// The blocks from the middle of a demonstration world's station to anything of a fortress.
constexpr int stationClearance = 40;

// The word /locate takes for a fortress.
constexpr QStringView locatableFortress = u"fortress";

quint64 netherSeed(qint64 seed)
{
    return qcMixSeed(static_cast<quint64>(seed), qcNameSalt(u"qtcraft:nether"_s));
}

} // namespace

QcNetherGenerator::QcNetherGenerator(const std::shared_ptr<const QcContentRegistry> &content, qint64 seed, std::optional<QcBlockPos> station)
    : m_definitions(*content, netherSeed(seed))
    , m_terrain(m_definitions, netherSeed(seed))
    , m_decorator(m_definitions, m_terrain)
    , m_deltas(m_definitions, m_terrain)
    , m_growth(m_definitions, m_terrain)
    , m_fortress(m_definitions, m_terrain, *content, netherSeed(seed))
    , m_structures(m_definitions, m_terrain, content, m_fortress)
{
    m_warnings = m_definitions.warnings + m_fortress.warnings();
    if (station) {
        m_structures.setStation(*station);
        // Its fortress stands beside it, the natural ones keep away from both.
        m_fortress.keepClearOf(QcXzRect{station->x, station->z, station->x, station->z}.grown(stationClearance));
        const int floor = std::clamp(station->y, m_definitions.settings.fortress.lowestFloor, m_definitions.settings.fortress.highestFloor);
        m_fortress.setDemonstration({station->x + stationClearance, floor, station->z - stationClearance / 2});
    }
}

std::shared_ptr<QcChunk> QcNetherGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    terrain().fill(*chunk);
    m_decorator.decorate(*chunk);
    m_deltas.shape(*chunk);
    m_growth.grow(*chunk);
    structures().build(*chunk);
    // Springs before the fortress, whose seals then close those beside its parts as well.
    m_decorator.placeSprings(*chunk);
    fortress().build(*chunk);
    return chunk;
}

QcBlockPos QcNetherGenerator::spawnPoint() const
{
    if (structures().station()) {
        const QcStructurePlacement &station = *structures().station();
        return {station.origin().x + station.sizeX() / 2, station.origin().y + station.structure().groundLayer, station.origin().z + station.sizeZ() / 2};
    }
    // Ring by ring around the origin: the first floor above the lava.
    for (int ring = 0; ring <= spawnRings; ++ring) {
        for (int step = 0; step < std::max(1, ring * 8); ++step) {
            const int side = step / std::max(1, ring * 2);
            const int along = step % std::max(1, ring * 2) - ring;
            const std::array<std::pair<int, int>, 4> sides{{{along, -ring}, {ring, along}, {-along, ring}, {-ring, -along}}};
            const auto &[dx, dz] = sides[static_cast<std::size_t>(side)];
            if (const std::optional<int> floor = terrain().floorAt(dx * spawnStep, dz * spawnStep, 0, spawnRoom)) {
                return {dx * spawnStep, *floor + 1, dz * spawnStep};
            }
        }
    }
    return {0, definitions().settings.liquidLevel + 2, 0};
}

QString QcNetherGenerator::biomeAt(int x, int z) const
{
    return definitions().biomes[terrain().biomeAt(x, z)].name;
}

bool QcNetherGenerator::isNaturalGround(QcBlockId block) const
{
    return definitions().isGround(block);
}

std::optional<QcStructurePlacement> QcNetherGenerator::nearestStructure(const QString &name, QcBlockPos from, int reach) const
{
    return structures().nearest(name, from, reach);
}

QcChunkFurnishing QcNetherGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing = structures().furnishingIn(pos);
    furnishing.add(fortress().furnishingIn(pos));
    return furnishing;
}

std::optional<QcStructureSites> QcNetherGenerator::plannedStructureSitesNear(QcBlockPos from, int reach) const
{
    return fortress().plannedSitesNear(from, reach);
}

QStringList QcNetherGenerator::locatableNames() const
{
    QStringList names = structures().names();
    names.append(locatableFortress.toString());
    return names;
}

QcLocateResult QcNetherGenerator::locate(QStringView kind, QcBlockPos from, int reach) const
{
    return kind == locatableFortress ? fortress().nearest(from, reach) : QcLocateResult{std::nullopt, reach};
}
