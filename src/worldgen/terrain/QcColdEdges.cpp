#include "worldgen/terrain/QcColdEdges.h"

#include <algorithm>
#include <cmath>

namespace {

// Horizontal scale of the ice's noise in blocks: lobes of the edge and floes beyond it.
constexpr double iceScale = 11.0;
// Share of the temperature noise that is per column (single floes and holes).
constexpr double iceScatter = 0.1;
// Horizontal scale of the ground's jitter in blocks, and the share of it that is per column.
constexpr double groundScale = 5.0;
constexpr double groundScatter = 0.35;

} // namespace

QcColdEdges::QcColdEdges(const QcTerrainDefinitions::ColdEdges &settings, quint64 seed)
    : m_settings(settings)
    , m_seed(qcMixSeed(seed, 0x7ce))
    , m_iceNoise(qcMixSeed(seed, 0x7cf))
    , m_groundNoiseX(qcMixSeed(seed, 0x7d0))
    , m_groundNoiseZ(qcMixSeed(seed, 0x7d1))
{
}

bool QcColdEdges::freezes(double temperature, bool river, int x, int z) const
{
    const double threshold = river ? m_settings.riverTemperature : m_settings.temperature;
    // Far from the threshold the noise cannot decide anything.
    if (temperature >= threshold + m_settings.ragged * 2.0) {
        return false;
    }
    if (temperature < threshold - m_settings.ragged * 2.0) {
        return true;
    }

    const double smooth = 1.6 * m_iceNoise.fractal2(x / iceScale, z / iceScale, 3);
    const double scatter = 2.0 * qcPositionRandom(m_seed, x, 1, z) - 1.0;
    const double shift = (1.0 - iceScatter) * smooth + iceScatter * scatter;
    return temperature + m_settings.ragged * std::clamp(shift, -2.0, 2.0) < threshold;
}

QcColdEdges::Offset QcColdEdges::groundOffset(int x, int z) const
{
    const int reach = m_settings.groundBlend;
    if (reach <= 0) {
        return {};
    }

    const auto along = [&](const QcNoise &noise, int salt) {
        const double smooth = 1.5 * noise.fractal2(x / groundScale, z / groundScale, 2);
        const double scatter = 2.0 * qcPositionRandom(m_seed, x, salt, z) - 1.0;
        const double shift = (1.0 - groundScatter) * smooth + groundScatter * scatter;
        return std::clamp(static_cast<int>(std::lround(reach * shift)), -reach, reach);
    };
    return {along(m_groundNoiseX, 2), along(m_groundNoiseZ, 3)};
}
