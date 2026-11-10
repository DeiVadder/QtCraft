// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherGenerator.h"

#include "worldgen/nether/QcNetherSpawn.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// The blocks from the middle of a demonstration world's station to anything of a fortress.
constexpr int stationClearance = 40;

// The blocks from the middle of a demonstration world's station to the row of its bastions.
constexpr int bastionClearance = 40 + QcBastionPlan::reach;

// The words /locate takes for a fortress and a bastion.
constexpr QStringView locatableFortress = u"fortress";
constexpr QStringView locatableBastion = u"bastion";

quint64 netherSeed(qint64 seed)
{
    return qcMixSeed(static_cast<quint64>(seed), qcNameSalt(u"qtcraft:nether"_s));
}

} // namespace

QcNetherGenerator::QcNetherGenerator(const std::shared_ptr<const QcContentRegistry> &content, qint64 seed, std::optional<QcBlockPos> station)
    : m_content(content)
    , m_definitions(*content, netherSeed(seed))
    , m_terrain(m_definitions, netherSeed(seed))
    , m_decorator(m_definitions, m_terrain)
    , m_deltas(m_definitions, m_terrain)
    , m_growth(m_definitions, m_terrain)
    , m_complexes(m_definitions, m_terrain, netherSeed(seed))
    , m_fortress(m_definitions, m_terrain, m_complexes, *content, netherSeed(seed))
    , m_bastion(m_definitions, m_terrain, m_complexes, *content, netherSeed(seed))
    , m_structures(m_definitions, m_terrain, content, m_fortress, m_bastion)
{
    m_warnings = m_definitions.warnings + m_fortress.warnings() + m_bastion.warnings();
    if (station) {
        m_structures.setStation(*station);
        setDemonstrations(*station);
    }
}

void QcNetherGenerator::setDemonstrations(QcBlockPos station)
{
    // Its fortress stands east of the station and its bastions west of it, the natural ones keep
    // away from all of them.
    const QcXzRect around = QcXzRect{station.x, station.z, station.x, station.z}.grown(stationClearance);
    fortressPlans().keepClearOf(around);
    bastionPlans().keepClearOf(around);
    const int floor = std::clamp(station.y, definitions().settings.fortress.lowestFloor, definitions().settings.fortress.highestFloor);
    fortressPlans().setDemonstration({station.x + stationClearance, floor, station.z - stationClearance / 2});
    if (const std::shared_ptr<const QcFortressPlan> shown = fortress().demonstration()) {
        bastionPlans().keepClearOf(shown->bounds);
    }
    bastionPlans().setDemonstration({station.x - bastionClearance, station.y, station.z});
    for (const std::shared_ptr<const QcBastionPlan> &plan : bastion().demonstrations()) {
        fortressPlans().keepClearOf(plan->bounds);
    }
}

std::shared_ptr<QcChunk> QcNetherGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    terrain().fill(*chunk);
    decorator().decorate(*chunk);
    m_deltas.shape(*chunk);
    m_growth.grow(*chunk);
    structures().build(*chunk);
    // Springs before the great structures, whose seals then close those beside their parts as well.
    decorator().placeSprings(*chunk);
    fortress().build(*chunk);
    bastion().build(*chunk);
    return chunk;
}

QcBlockPos QcNetherGenerator::spawnPoint() const
{
    if (structures().station()) {
        const QcStructurePlacement &station = *structures().station();
        return {station.origin().x + station.sizeX() / 2, station.origin().y + station.structure().groundLayer, station.origin().z + station.sizeZ() / 2};
    }
    // Searched once (it generates chunks), on the blocks the generator places (decision E-1111).
    std::call_once(m_spawnOnce, [this] { m_spawn = QcNetherSpawn(*this, *m_content).find(); });
    return m_spawn;
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
    furnishing.add(bastion().furnishingIn(pos));
    return furnishing;
}

QcStructureSites QcNetherGenerator::structureSitesNear(QcBlockPos from, int reach) const
{
    QcStructureSites sites = fortress().sitesNear(from, reach);
    sites.add(bastion().sitesNear(from, reach));
    return sites;
}

std::optional<QcStructureSites> QcNetherGenerator::plannedStructureSitesNear(QcBlockPos from, int reach) const
{
    std::optional<QcStructureSites> sites = fortress().plannedSitesNear(from, reach);
    const std::optional<QcStructureSites> bastions = bastion().plannedSitesNear(from, reach);
    if (!sites || !bastions) {
        return std::nullopt;
    }
    sites->add(*bastions);
    return sites;
}

QStringList QcNetherGenerator::locatableNames() const
{
    QStringList names = structures().names();
    names.append(locatableFortress.toString());
    names.append(locatableBastion.toString());
    return names;
}

std::optional<QString> QcNetherGenerator::structureAt(QcBlockPos at) const
{
    const std::optional<bool> inFortress = fortress().plannedHolds(at);
    const std::optional<bool> inBastion = bastion().plannedHolds(at);
    if (inFortress.value_or(false)) {
        return locatableFortress.toString();
    }
    if (inBastion.value_or(false)) {
        return locatableBastion.toString();
    }
    return inFortress && inBastion ? std::optional(QString()) : std::nullopt;
}

QcLocateResult QcNetherGenerator::locate(QStringView kind, QcBlockPos from, int reach) const
{
    if (kind == locatableBastion) {
        return bastion().nearest(from, reach);
    }
    return kind == locatableFortress ? fortress().nearest(from, reach) : QcLocateResult{std::nullopt, reach};
}
