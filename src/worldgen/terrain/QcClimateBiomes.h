// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcTerrainClimate.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

/*! \brief The biome of a climate point (decisions E-014, E-821, E-824): inverse-square weights
    of the distance in climate space, less each biome's spread, gated for rare biomes by the
    rarity map. */
namespace QcClimateBiomes {

/*! The biome definition of the terrain generator. */
using Biome = QcTerrainDefinitions::Biome;

/*! Returns the squared distance of the point \a climate to the climate of \a biome (ruggedness
    counted twice), less the biome's spread, plus a small constant: positive and strictly ordered,
    also inside the spread core. */
[[nodiscard]] double distance(const Biome &biome, const QcTerrainClimate::Point &climate);
/*! Returns the inverse-square weight of \a biome at the point \a climate, before the rarity
    gate. */
[[nodiscard]] double weight(const Biome &biome, const QcTerrainClimate::Point &climate);
/*! Returns the share of the weight of \a biome at a column's \a rarity: \a rarity divided by
    the biome's rarity, at most 1; 1 for an ordinary biome. */
[[nodiscard]] double rarityGate(const Biome &biome, double rarity);
/*! Returns the index in \a biomes of the heaviest biome chosen by climate at the point
    \a climate after the rarity gate; the nearest one when every biome is rare and the point lies
    outside the spots (decision E-815). Biomes not chosen by climate are skipped. */
[[nodiscard]] int choose(const QList<Biome> &biomes, const QcTerrainClimate::Point &climate);

} // namespace QcClimateBiomes
