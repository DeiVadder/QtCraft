// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/strongholds/QcStrongholdRings.h"

#include "common/core/QcIntMath.h"
#include "common/core/QcWorldConstants.h"
#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace {

// The steps a stronghold moves by to find land: a chunk.
constexpr int nudgeStep = QcWorld::chunkSize;

// The offsets within the reach of a nudge, the nearest first.
QList<std::pair<int, int>> offsetsWithinReach()
{
    QList<std::pair<int, int>> list;
    constexpr int steps = QcStrongholdRings::nudgeReach / nudgeStep;
    for (int dz = -steps; dz <= steps; ++dz) {
        for (int dx = -steps; dx <= steps; ++dx) {
            if (dx * dx + dz * dz <= steps * steps) {
                list.append({dx * nudgeStep, dz * nudgeStep});
            }
        }
    }
    std::ranges::stable_sort(list, {}, [](std::pair<int, int> offset) { return offset.first * offset.first + offset.second * offset.second; });
    return list;
}

// The offsets a stronghold may move by, the nearest first.
const QList<std::pair<int, int>> &nudges()
{
    static const QList<std::pair<int, int>> offsets = offsetsWithinReach();
    return offsets;
}

// The distances from the origin to the nearest and the farthest column of `area`.
std::pair<double, double> distancesOf(QcXzRect area)
{
    const auto nearAxis = [](int low, int high) { return low > 0 ? low : (high < 0 ? -high : 0); };
    const auto farAxis = [](int low, int high) { return std::max(std::abs(low), std::abs(high)); };
    const double nearX = nearAxis(area.minX, area.maxX);
    const double nearZ = nearAxis(area.minZ, area.maxZ);
    const double farX = farAxis(area.minX, area.maxX);
    const double farZ = farAxis(area.minZ, area.maxZ);
    return {std::hypot(nearX, nearZ), std::hypot(farX, farZ)};
}

} // namespace

QcStrongholdRings::QcStrongholdRings(quint64 seed, LandTest land)
    : m_seed(qcMixSeed(seed, 0x57f0))
    , m_land(std::move(land))
{
}

int QcStrongholdRings::ringOf(int index)
{
    int ring = 0;
    while (ring + 1 < static_cast<int>(rings.size()) && index >= firstOf(ring + 1)) {
        ++ring;
    }
    return ring;
}

int QcStrongholdRings::firstOf(int ring)
{
    int first = 0;
    for (int i = 0; i < ring; ++i) {
        first += rings[static_cast<std::size_t>(i)].count;
    }
    return first;
}

QcBlockPos QcStrongholdRings::rawPosition(quint64 seed, int ring, int i)
{
    const Ring &shape = rings[static_cast<std::size_t>(ring)];
    const double start = qcPositionRandom(seed, ring, -1, 0) * 2.0 * std::numbers::pi;
    const double angle = start + i * 2.0 * std::numbers::pi / shape.count;
    const double distance = shape.minDistance + qcPositionRandom(seed, ring, i, 1) * (shape.maxDistance - shape.minDistance);
    // The middle of the chunk it falls in.
    const int x = static_cast<int>(std::floor(std::cos(angle) * distance));
    const int z = static_cast<int>(std::floor(std::sin(angle) * distance));
    const int half = QcWorld::chunkSize / 2;
    return {qcFloorDiv(x, QcWorld::chunkSize) * QcWorld::chunkSize + half, 0, qcFloorDiv(z, QcWorld::chunkSize) * QcWorld::chunkSize + half};
}

QList<QcBlockPos> QcStrongholdRings::place(int ring) const
{
    QList<QcBlockPos> positions;
    const int count = rings[static_cast<std::size_t>(ring)].count;
    for (int i = 0; i < count; ++i) {
        const QcBlockPos raw = rawPosition(seed(), ring, i);
        // The nearest land, or where it was when none lies within reach (a stronghold under the
        // sea, as in the original game).
        const auto land = std::ranges::find_if(nudges(), [&](std::pair<int, int> offset) { return m_land(raw.x + offset.first, raw.z + offset.second); });
        positions.append(land == nudges().end() ? raw : raw.offset(land->first, 0, land->second));
    }
    return positions;
}

std::optional<QList<QcBlockPos>> QcStrongholdRings::known(int ring) const
{
    const std::scoped_lock lock(m_mutex);
    return m_rings[static_cast<std::size_t>(ring)];
}

QList<QcBlockPos> QcStrongholdRings::positions(int ring) const
{
    if (std::optional<QList<QcBlockPos>> cached = known(ring)) {
        return *std::move(cached);
    }
    // Worked out outside the lock (it reads the terrain); two threads at once get the same.
    QList<QcBlockPos> placed = place(ring);
    const std::scoped_lock lock(m_mutex);
    m_rings[static_cast<std::size_t>(ring)] = placed;
    return placed;
}

QcBlockPos QcStrongholdRings::positionOf(int index) const
{
    const int ring = ringOf(index);
    return positions(ring).at(index - firstOf(ring));
}

QList<int> QcStrongholdRings::ringsNear(QcXzRect area, int margin)
{
    const auto [nearest, farthest] = distancesOf(area);
    QList<int> found;
    for (std::size_t ring = 0; ring < rings.size(); ++ring) {
        const int reach = nudgeReach + margin + QcWorld::chunkSize;
        if (farthest >= rings[ring].minDistance - reach && nearest <= rings[ring].maxDistance + reach) {
            found.append(static_cast<int>(ring));
        }
    }
    return found;
}

QList<int> QcStrongholdRings::within(const QList<QcBlockPos> &positions, int ring, QcXzRect area, int margin)
{
    QList<int> found;
    const QcXzRect grown = area.grown(margin);
    for (qsizetype i = 0; i < positions.size(); ++i) {
        if (grown.contains(positions[i].x, positions[i].z)) {
            found.append(firstOf(ring) + static_cast<int>(i));
        }
    }
    return found;
}

QList<int> QcStrongholdRings::near(QcXzRect area, int margin) const
{
    QList<int> found;
    const QList<int> asked = ringsNear(area, margin);
    for (const int ring : asked) {
        found += within(positions(ring), ring, area, margin);
    }
    return found;
}

std::optional<QList<int>> QcStrongholdRings::knownNear(QcXzRect area, int margin) const
{
    QList<int> found;
    const QList<int> asked = ringsNear(area, margin);
    for (const int ring : asked) {
        const std::optional<QList<QcBlockPos>> placed = known(ring);
        if (!placed) {
            return std::nullopt;
        }
        found += within(*placed, ring, area, margin);
    }
    return found;
}

int QcStrongholdRings::nearest(QcBlockPos from) const
{
    return nearest(from, [](int) { return true; }).value_or(0);
}

std::optional<int> QcStrongholdRings::nearest(QcBlockPos from, const std::function<bool(int index)> &accept) const
{
    // The rings by how near a stronghold of theirs may lie, the nearest first. A stronghold of the
    // rings worked out so far is asked once no further ring can hold one nearer.
    const double fromDistance = std::hypot(static_cast<double>(from.x), static_cast<double>(from.z));
    QList<std::pair<double, int>> order;
    for (std::size_t ring = 0; ring < rings.size(); ++ring) {
        const double inside = rings[ring].minDistance - nudgeReach - fromDistance;
        const double outside = fromDistance - rings[ring].maxDistance - nudgeReach;
        order.append({std::max({0.0, inside, outside}), static_cast<int>(ring)});
    }
    std::ranges::sort(order);

    QList<std::pair<double, int>> candidates;
    for (qsizetype k = 0; k < order.size(); ++k) {
        const int ring = order[k].second;
        const QList<QcBlockPos> placed = positions(ring);
        for (qsizetype i = 0; i < placed.size(); ++i) {
            const double distance = std::hypot(static_cast<double>(placed[i].x - from.x), static_cast<double>(placed[i].z - from.z));
            candidates.append({distance, firstOf(ring) + static_cast<int>(i)});
        }
        std::ranges::sort(candidates);
        const double nextBound = k + 1 < order.size() ? order[k + 1].first : std::numeric_limits<double>::infinity();
        while (!candidates.isEmpty() && candidates.constFirst().first <= nextBound) {
            const int index = candidates.takeFirst().second;
            if (accept(index)) {
                return index;
            }
        }
    }
    return std::nullopt;
}
