// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcTerrainClimate.h"
#include "worldgen/terrain/QcTerrainDefinitions.h"

// The biome of a climate point (decisions E-014, E-821, E-824): inverse-square weights of the
// distance in climate space, less each biome's spread, gated for rare biomes by the rarity map.
namespace QcClimateBiomes {

using Biome = QcTerrainDefinitions::Biome;

// The squared distance of a climate point to the biome's, less the biome's spread, plus a small
// constant: positive and strictly ordered, also inside the spread core.
[[nodiscard]] double distance(const Biome &biome, const QcTerrainClimate::Point &climate);
// The inverse-square weight of a biome at a climate point, before the rarity gate.
[[nodiscard]] double weight(const Biome &biome, const QcTerrainClimate::Point &climate);
// The share of a rare biome's weight at a column's rarity (1 for an ordinary biome).
[[nodiscard]] double rarityGate(const Biome &biome, double rarity);
// The index of the heaviest biome after the rarity gate; the nearest one when every biome is rare
// and the point lies outside the spots (decision E-815).
[[nodiscard]] int choose(const QList<Biome> &biomes, const QcTerrainClimate::Point &climate);

} // namespace QcClimateBiomes
