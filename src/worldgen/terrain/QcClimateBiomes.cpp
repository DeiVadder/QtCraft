// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcClimateBiomes.h"

#include <algorithm>
#include <cmath>

namespace {

// The ruggedness axis counts twice as much as temperature and humidity.
constexpr double ruggednessWeight = 2.0;
// Added to the squared climate distance, so a biome's weight stays finite at its own climate point.
constexpr double climateNear = 0.0015;

} // namespace

namespace QcClimateBiomes {

double distance(const Biome &biome, const QcTerrainClimate::Point &climate)
{
    const double dt = climate.temperature - biome.temperature;
    const double dh = climate.humidity - biome.humidity;
    const double dr = ruggednessWeight * (climate.ruggedness - biome.ruggedness);
    const double power = dt * dt + dh * dh + dr * dr - biome.spread;
    // Inside a spread biome's core the power is negative: it stays strictly ordered there (two
    // spread biomes overlapping still meet on a straight border, and no tie falls to the biome
    // listed first), positive, and continuous with the same slope at 0.
    return power > 0.0 ? power + climateNear : climateNear * std::exp(power / climateNear);
}

double weight(const Biome &biome, const QcTerrainClimate::Point &climate)
{
    const double far = distance(biome, climate);
    return 1.0 / (far * far);
}

double rarityGate(const Biome &biome, double rarity)
{
    // Gated by the rarity map (decision E-821): exactly 0 outside the spots, so the other biomes'
    // heights stay untouched there, and full weight where the column's rarity reaches the biome's.
    return biome.rarity > 0.0 ? std::min(1.0, rarity / biome.rarity) : 1.0;
}

int choose(const QList<Biome> &biomes, const QcTerrainClimate::Point &climate)
{
    double bestWeight = 0.0;
    int best = -1;
    double nearestWeight = -1.0;
    int nearest = 0;
    for (qsizetype i = 0; i < biomes.size(); ++i) {
        const Biome &biome = biomes[i];
        if (!biome.byClimate()) {
            continue;
        }
        const double own = weight(biome, climate);
        if (own > nearestWeight) {
            nearestWeight = own;
            nearest = static_cast<int>(i);
        }
        const double gated = own * rarityGate(biome, climate.rarity);
        if (gated > bestWeight) {
            bestWeight = gated;
            best = static_cast<int>(i);
        }
    }
    // Every biome rare and the point outside the spots (decision E-815): the nearest biome.
    return best >= 0 ? best : nearest;
}

} // namespace QcClimateBiomes
