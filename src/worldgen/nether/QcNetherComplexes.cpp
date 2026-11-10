// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherComplexes.h"

#include "worldgen/bastion/QcBastionPlan.h"
#include "worldgen/nether/QcNetherDefinitions.h"
#include "worldgen/nether/QcNetherTerrain.h"
#include "worldgen/terrain/QcNoise.h"

namespace {

// Cells between a bastion's reach and the edge of its region (QcNetherParser keeps room for it).
constexpr int regionMargin = QcBastionPlan::regionMargin;

} // namespace

QcNetherComplexes::QcNetherComplexes(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain, quint64 seed)
    : m_definitions(definitions)
    , m_terrain(terrain)
    , m_seed(qcMixSeed(seed, 0xf047))
{
}

QcBlockPos QcNetherComplexes::bastionSite(int regionX, int regionZ) const
{
    const QcNetherSettings::Bastion &bastion = m_definitions.settings.bastion;
    const int room = bastion.regionSize - 2 * (QcBastionPlan::reach + regionMargin);
    const int edge = QcBastionPlan::reach + regionMargin;
    const int x = regionX * bastion.regionSize + edge + static_cast<int>(qcPositionRandom(seed(), regionX, 1, regionZ) * room);
    const int z = regionZ * bastion.regionSize + edge + static_cast<int>(qcPositionRandom(seed(), regionX, 2, regionZ) * room);
    return {x, bastion.floor, z};
}

QcNetherComplexes::Kind QcNetherComplexes::kindOf(int regionX, int regionZ) const
{
    const QcNetherSettings &settings = m_definitions.settings;
    const double draw = qcPositionRandom(seed(), regionX, 0, regionZ);
    const double fortress = settings.fortress.regionSize > 0 ? settings.fortress.chance : 0.0;
    if (draw < fortress) {
        return Kind::Fortress;
    }
    if (settings.bastion.regionSize <= 0 || draw >= fortress + settings.bastion.chance) {
        return Kind::None;
    }
    const QcBlockPos site = bastionSite(regionX, regionZ);
    if (settings.bastion.biomes.contains(m_definitions.biomes[m_terrain.biomeAt(site.x, site.z)].name)) {
        return Kind::Bastion;
    }
    return settings.fortress.regionSize > 0 ? Kind::Fortress : Kind::None;
}
