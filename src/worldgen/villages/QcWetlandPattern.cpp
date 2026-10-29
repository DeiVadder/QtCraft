// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/villages/QcWetlandPattern.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

// Octaves of the pool noise: two give pools with bays and narrow channels between them.
constexpr int poolOctaves = 2;
// The share of a pool's area (of the water share) that lies a block deeper, around its middle.
constexpr double deepShare = 0.35;
// Grid of noise samples the quantiles are taken from, and their spacing in noise units (several
// samples per noise cell, far more cells than a village covers).
constexpr int sampleGrid = 96;
constexpr double sampleSpacing = 0.37;

bool onPieceOrBoardwalk(const QcVillagePlan::Wetland &wetland, int x, int z)
{
    return std::ranges::any_of(wetland.wet, [x, z](QcVillagePlan::Area area) { return area.contains(x, z); });
}

} // namespace

QcWetlandPattern::QcWetlandPattern(quint64 seed)
    : m_noise(qcMixSeed(seed, qcNameSalt(u"qtcraft:village_wetland_pools"_s)))
    , m_plantSeed(qcMixSeed(seed, qcNameSalt(u"qtcraft:village_wetland_plants"_s)))
{
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(sampleGrid) * sampleGrid);
    for (int j = 0; j < sampleGrid; ++j) {
        for (int i = 0; i < sampleGrid; ++i) {
            samples.push_back(m_noise.fractal2(i * sampleSpacing, j * sampleSpacing, poolOctaves));
        }
    }
    std::ranges::sort(samples);

    for (int step = 0; step <= quantileSteps; ++step) {
        const std::size_t index = std::min(samples.size() - 1, samples.size() * static_cast<std::size_t>(step) / quantileSteps);
        m_quantiles[static_cast<std::size_t>(step)] = samples[index];
    }
}

double QcWetlandPattern::quantile(double share) const
{
    if (share <= 0.0) {
        return -1e9;
    }
    if (share >= 1.0) {
        return 1e9;
    }
    // Linear between the percent samples.
    const double at = share * quantileSteps;
    const int low = static_cast<int>(std::floor(at));
    const double part = at - low;
    return m_quantiles[static_cast<std::size_t>(low)] + part * (m_quantiles[static_cast<std::size_t>(low) + 1] - m_quantiles[static_cast<std::size_t>(low)]);
}

double QcWetlandPattern::poolNoise(const QcVillageStyle::Wetland &settings, int x, int z) const
{
    return m_noise.fractal2(x / settings.scale, z / settings.scale, poolOctaves);
}

bool QcWetlandPattern::isPool(const QcVillagePlan::Wetland &wetland, int x, int z, double threshold) const
{
    return poolNoise(wetland.settings, x, z) < threshold && !onPieceOrBoardwalk(wetland, x, z);
}

bool QcWetlandPattern::nearPool(const QcVillagePlan::Wetland &wetland, int x, int z, double threshold) const
{
    const int band = wetland.settings.mudBand;
    for (int dz = -band; dz <= band; ++dz) {
        for (int dx = -band; dx <= band; ++dx) {
            if (dx * dx + dz * dz <= band * band && isPool(wetland, x + dx, z + dz, threshold)) {
                return true;
            }
        }
    }
    return false;
}

QcWetlandPattern::Cover QcWetlandPattern::coverAt(const QcVillagePlan::Wetland &wetland, int x, int z) const
{
    if (onPieceOrBoardwalk(wetland, x, z)) {
        return Cover::Mud;
    }

    const QcVillageStyle::Wetland &settings = wetland.settings;
    const double threshold = quantile(settings.water);
    const double noise = poolNoise(settings, x, z);
    if (noise < quantile(settings.water * deepShare)) {
        return Cover::DeepWater;
    }
    if (noise < threshold) {
        return Cover::Water;
    }
    return nearPool(wetland, x, z, threshold) ? Cover::Mud : Cover::Grass;
}

bool QcWetlandPattern::plantAt(const QcVillageStyle::Wetland &settings, int x, int z) const
{
    return settings.plant != qcAirBlockId && plantDraw(x, z) < settings.plantChance;
}

bool QcWetlandPattern::floatingPlantAt(const QcVillageStyle::Wetland &settings, int x, int z) const
{
    // Pool columns never carry the plant beside the pools, so both use the same draw.
    return settings.floatingPlant != qcAirBlockId && plantDraw(x, z) < settings.floatingPlantChance;
}

double QcWetlandPattern::plantDraw(int x, int z) const
{
    const quint64 hash = qcMixSeed(qcMixSeed(m_plantSeed, static_cast<quint32>(x)), static_cast<quint32>(z));
    return static_cast<double>(hash >> 11) * 0x1.0p-53;
}
