// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcNoise.h"

#include <QtGlobal>

// The climate maps of the terrain (decisions E-014, E-101, E-821): temperature, humidity,
// ruggedness and rarity of a column, read at coordinates bent a second time on a smaller scale so
// biome borders fray into lobes and tongues. Deterministic per seed and thread-safe.
class QcTerrainClimate
{
public:
    // The climate point of a column: temperature, humidity and ruggedness in [0, 1], and the rarity
    // (decision E-821), 0 outside the spots of the rarity map and everywhere without `withRarity`.
    struct Point
    {
        double temperature = 0.5;
        double humidity = 0.5;
        double ruggedness = 0.0;
        double rarity = 0.0;
    };

    // `withRarity`: some biome is rare; otherwise the rarity map is not computed. `biomeScale`
    // stretches the temperature, humidity, ruggedness and rarity maps (QcTerrainSettings::biomeScale);
    // at 1 every point is exactly as without it.
    QcTerrainClimate(quint64 seed, bool withRarity, double biomeScale = 1.0);

    // `warpedX`, `warpedZ`: the column bent by the terrain's large-scale warp, shared with the coasts.
    [[nodiscard]] Point at(int x, int z, double warpedX, double warpedZ) const;

private:
    bool m_withRarity = false;
    double m_temperatureScale = 0.0;
    double m_humidityScale = 0.0;
    double m_ruggednessScale = 0.0;
    double m_rarityScale = 0.0;
    QcNoise m_temperatureNoise;
    QcNoise m_humidityNoise;
    QcNoise m_ruggednessNoise;
    QcNoise m_climateWarpNoiseX;
    QcNoise m_climateWarpNoiseZ;
    QcNoise m_rarityNoise;
};
