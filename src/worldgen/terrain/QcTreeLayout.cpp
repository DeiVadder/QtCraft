// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcTreeLayout.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>

namespace {

// North, east, south and west, the order of QcTreeShape::Params::vines.
constexpr std::array<std::pair<int, int>, 4> sides{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};
constexpr double trunkVineChance = 0.3;
constexpr double leafVineChance = 0.35;
// Salts of the random values of one tree.
constexpr quint64 vineSalt = 0x7615E5;
constexpr quint64 branchSalt = 0xB4A2C1;
constexpr quint64 raggedSalt = 0x4A66ED;
// Branches of jungle giants: blocks out from the trunk, and the leaves around their end.
constexpr int branchLength = 2;
constexpr int branchLeafRadius = 2;

// Squared distance of (dx, dz) from the 2×2 trunk covering (0..1, 0..1).
int giantDistance2(int dx, int dz)
{
    const int ex = dx < 0 ? -dx : std::max(0, dx - 1);
    const int ez = dz < 0 ? -dz : std::max(0, dz - 1);
    return ex * ex + ez * ez;
}

} // namespace

QcTreeLayout::QcTreeLayout(const QcTreeShape::Params &params, const QcTreeShape::Put &put)
    : m_put(put)
    , m_tracks(params.vines[0] != qcAirBlockId)
{
}

void QcTreeLayout::log(QcBlockPos pos, QcBlockId block, bool holdsVines)
{
    const bool placed = m_put(pos, block, false);
    remember(pos, holdsVines && placed ? trunkVineChance : 0.0);
}

void QcTreeLayout::leaf(QcBlockPos pos, QcBlockId block, bool holdsVines)
{
    // A leaf refused by what stands there holds no vines.
    const bool placed = m_put(pos, block, true);
    remember(pos, placed ? (holdsVines ? leafVineChance : m_leafVineChance) : 0.0);
}

void QcTreeLayout::remember(QcBlockPos pos, double vineChance)
{
    if (!m_tracks) {
        return;
    }

    m_occupied.insert(pos);
    if (vineChance > 0.0) {
        m_holders.append({pos, vineChance});
    }
}

void QcTreeLayout::hangVines(const QcTreeShape::Params &params, quint64 seed) const
{
    if (!m_tracks) {
        return;
    }

    const quint64 vineSeed = qcMixSeed(seed, vineSalt);
    for (const auto &[holder, chance] : m_holders) {
        for (std::size_t side = 0; side < sides.size(); ++side) {
            const QcBlockPos pos = holder.offset(sides[side].first, 0, sides[side].second);
            if (m_occupied.contains(pos) || qcPositionRandom(vineSeed, pos.x * 4 + static_cast<int>(side), pos.y, pos.z) >= chance) {
                continue;
            }
            // The vine holds on to `holder`, which lies on the opposite side of it.
            static_cast<void>(m_put(pos, params.vines[(side + 2) % sides.size()], true));
        }
    }
}

void QcTreeLayout::giantLayer(const QcTreeShape::Params &params, QcBlockPos base, int radius, quint64 seed)
{
    const quint64 raggedSeed = qcMixSeed(seed, raggedSalt);
    const int outer = radius * radius + radius;
    const int inner = (radius - 1) * (radius - 1) + (radius - 1);
    for (int dx = -radius; dx <= radius + 1; ++dx) {
        for (int dz = -radius; dz <= radius + 1; ++dz) {
            const int distance = giantDistance2(dx, dz);
            if (distance > outer || (radius > 1 && distance > inner && qcPositionRandom(raggedSeed, base.x + dx, base.y, base.z + dz) < 0.3)) {
                continue;
            }
            leaf(base.offset(dx, 0, dz), params.leaves, false);
        }
    }
}

void QcTreeLayout::buildGiantCone(const QcTreeShape::Params &params, QcBlockPos base, int trunkHeight, quint64 seed)
{
    // Layer 0 caps the trunk; going down the rings widen by one every third layer, every other
    // layer a step narrower, so the crown looks layered like a spruce.
    const int crownLayers = std::max(4, trunkHeight * 3 / 5);
    const int top = base.y + trunkHeight;
    for (int layer = 0; layer < crownLayers; ++layer) {
        const int widest = std::min(giantCrownRadius, (layer + 1) / 3);
        const int radius = std::max(0, widest - (layer % 2 == 1 && layer > 2 ? 1 : 0));
        giantLayer(params, {base.x, top - layer, base.z}, radius, seed);
    }
}

void QcTreeLayout::branch(const QcTreeShape::Params &params, QcBlockPos start, int side, quint64 seed)
{
    const auto [stepX, stepZ] = sides[static_cast<std::size_t>(side)];
    const QcBlockId lying = stepX != 0 ? params.trunkX : params.trunkZ;
    QcBlockPos end = start;
    for (int step = 1; step <= branchLength; ++step) {
        end = start.offset(stepX * step, step / 2, stepZ * step);
        log(end, lying, false);
    }

    const quint64 leafSeed = qcMixSeed(seed, branchSalt + 1);
    for (int dy = 0; dy <= 1; ++dy) {
        squareLayer(end.offset(0, dy, 0), branchLeafRadius - dy, params.leaves, {leafSeed, 0.7});
    }
}

void QcTreeLayout::squareLayer(QcBlockPos centre, int radius, QcBlockId leaves, Corners corners)
{
    const int side = 2 * radius + 1;
    for (int cell = 0; cell < side * side; ++cell) {
        // The cells of the square as one index.
        const int dx = cell % side - radius;
        const int dz = cell / side - radius;
        const bool corner = radius > 0 && std::abs(dx) == radius && std::abs(dz) == radius;
        if (!corner || qcPositionRandom(corners.seed, centre.x + dx, centre.y, centre.z + dz) >= corners.dropChance) {
            leaf(centre.offset(dx, 0, dz), leaves, false);
        }
    }
}

void QcTreeLayout::buildGiantRound(const QcTreeShape::Params &params, QcBlockPos base, int trunkHeight, quint64 seed)
{
    // Branches every two to four blocks on the upper half of the trunk, each from one of the trunk
    // columns on its side.
    const quint64 branchSeed = qcMixSeed(seed, branchSalt);
    const int top = base.y + trunkHeight - 1;
    for (int y = base.y + trunkHeight / 2; y < top - 3;) {
        const auto random = [&](int salt) { return qcPositionRandom(branchSeed, base.x + salt, y, base.z); };
        const int side = static_cast<int>(random(0) * 4.0);
        const int along = random(1) < 0.5 ? 0 : 1;
        static constexpr std::array<std::pair<int, int>, 4> edges{{{-1, 0}, {1, -1}, {-1, 1}, {0, -1}}};
        // The trunk column on that side: x or z fixed by the side, the other one `along`.
        const auto [fixedX, fixedZ] = edges[static_cast<std::size_t>(side)];
        const QcBlockPos start{base.x + (fixedX < 0 ? along : fixedX), y, base.z + (fixedZ < 0 ? along : fixedZ)};
        branch(params, start, side, seed);
        y += 2 + static_cast<int>(random(2) * 3.0);
    }

    // The crown: a narrower layer with vines (within the reach beside it) under two wide layers, then
    // narrower ones.
    static constexpr std::array<int, 4> radii{giantCrownRadius - 1, giantCrownRadius, giantCrownRadius - 1, 1};
    for (std::size_t layer = 0; layer < radii.size(); ++layer) {
        m_leafVineChance = layer == 0 ? leafVineChance : 0.0;
        giantLayer(params, {base.x, top - 1 + static_cast<int>(layer), base.z}, radii[layer], seed);
    }
    m_leafVineChance = 0.0;
}
