// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcTerrainClimate.h"

#include "worldgen/terrain/QcTerrainMath.h"

#include <algorithm>
#include <cmath>

using QcTerrainMath::smoothstep;

namespace {

// Horizontal scales of the climate maps in blocks, at the biome scale 1 (decision E-917).
constexpr double temperatureScale = 1900.0;
constexpr double humidityScale = 1600.0;
constexpr double ruggednessScale = 1500.0;
// The climate maps are bent a second time on a smaller scale, which frays biome borders into lobes
// and tongues instead of round patches (decision E-101).
constexpr double climateWarpScale = 160.0;
constexpr double climateWarpAmplitude = 200.0;
// Temperature and humidity take only this share of that bend (decision E-824): the full bend folds
// the map in places, where the climate then changes by a third of its range within a few dozen
// blocks and a cold biome comes close to a hot one.
constexpr double climateFray = 0.5;
// Spreads the climate noise (about +-0.3 around 0) over most of [0, 1].
constexpr double climateContrast = 1.5;
// Ruggedness only rises where the ruggedness noise is near zero: long winding chains.
constexpr double ruggednessSharpness = 6.0;
// Rarity (decision E-821): spots of about rarityScale blocks where the noise rises from rarityLow
// to rarityHigh; exactly 0 everywhere else.
constexpr double rarityScale = 520.0;
constexpr double rarityLow = 0.09;
constexpr double rarityHigh = 0.18;

} // namespace

QcTerrainClimate::QcTerrainClimate(quint64 seed, bool withRarity, double biomeScale)
    : m_withRarity(withRarity)
    , m_temperatureScale(temperatureScale * biomeScale)
    , m_humidityScale(humidityScale * biomeScale)
    , m_ruggednessScale(ruggednessScale * biomeScale)
    , m_rarityScale(rarityScale * biomeScale)
    , m_temperatureNoise(qcMixSeed(seed, 3))
    , m_humidityNoise(qcMixSeed(seed, 4))
    , m_ruggednessNoise(qcMixSeed(seed, 8))
    , m_climateWarpNoiseX(qcMixSeed(seed, 18))
    , m_climateWarpNoiseZ(qcMixSeed(seed, 19))
    , m_rarityNoise(qcMixSeed(seed, 20))
{
}

QcTerrainClimate::Point QcTerrainClimate::at(int x, int z, double warpedX, double warpedZ) const
{
    const double climateX = warpedX + climateWarpAmplitude * m_climateWarpNoiseX.fractal2(x / climateWarpScale, z / climateWarpScale, 3);
    const double climateZ = warpedZ + climateWarpAmplitude * m_climateWarpNoiseZ.fractal2(x / climateWarpScale, z / climateWarpScale, 3);
    const double frayedX = warpedX + climateFray * (climateX - warpedX);
    const double frayedZ = warpedZ + climateFray * (climateZ - warpedZ);
    Point point;
    point.temperature = std::clamp(
        0.5 + climateContrast * m_temperatureNoise.fractal2(frayedX / m_temperatureScale, frayedZ / m_temperatureScale, 3), 0.0, 1.0);
    point.humidity =
        std::clamp(0.5 + climateContrast * m_humidityNoise.fractal2(frayedX / m_humidityScale, frayedZ / m_humidityScale, 3), 0.0, 1.0);
    point.ruggedness = std::clamp(
        1.0 - ruggednessSharpness * std::abs(m_ruggednessNoise.fractal2(climateX / m_ruggednessScale, climateZ / m_ruggednessScale, 3)),
        0.0, 1.0);

    // Rarity: spots where the noise peaks, bent by the climate warp like the other maps, so rare
    // biomes have frayed borders too.
    if (m_withRarity) {
        point.rarity = smoothstep(rarityLow, rarityHigh, m_rarityNoise.fractal2(climateX / m_rarityScale, climateZ / m_rarityScale, 2));
    }
    return point;
}
