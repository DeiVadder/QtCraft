#pragma once

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/villages/QcVillagePlan.h"

#include <array>

// The surface of a wetland (decision E-661): irregular pools of water where a seeded noise lies
// below the quantile of the style's water share, the deepest part of them a block deeper, a band of
// the bottom block (mud) around them, grass between. Under pieces and boardwalks the ground is mud
// (no pools), so posts stand on it. A pure function of the seed, the plan and the column, so every
// chunk sees the same pattern in any order. Thread-safe.
class QcWetlandPattern
{
public:
    enum class Cover
    {
        Grass,
        Mud,
        Water,
        DeepWater,
    };

    explicit QcWetlandPattern(quint64 seed);

    // What covers the flat wetland at the column.
    [[nodiscard]] Cover coverAt(const QcVillagePlan::Wetland &wetland, int x, int z) const;
    // Whether the column's plant (if the style has one) grows there, a pure function of the column.
    [[nodiscard]] bool plantAt(const QcVillageStyle::Wetland &settings, int x, int z) const;
    // Whether the floating plant (if the style has one) lies on the water of a pool column.
    [[nodiscard]] bool floatingPlantAt(const QcVillageStyle::Wetland &settings, int x, int z) const;

private:
    // The column's draw in [0, 1) for its plants.
    [[nodiscard]] double plantDraw(int x, int z) const;
    // The noise value below which the given share of all columns lies.
    [[nodiscard]] double quantile(double share) const;
    // The pool noise at the column (scaled by the style's noise scale).
    [[nodiscard]] double poolNoise(const QcVillageStyle::Wetland &settings, int x, int z) const;
    // Whether the column is a pool: its noise below the water share's quantile, off pieces and boardwalks.
    [[nodiscard]] bool isPool(const QcVillagePlan::Wetland &wetland, int x, int z, double threshold) const;
    // Whether a pool lies within the mud band of the column.
    [[nodiscard]] bool nearPool(const QcVillagePlan::Wetland &wetland, int x, int z, double threshold) const;

    // Samples of the noise distribution: the value at every percent.
    static constexpr int quantileSteps = 100;

    QcNoise m_noise;
    quint64 m_plantSeed;
    std::array<double, quantileSteps + 1> m_quantiles{};
};
