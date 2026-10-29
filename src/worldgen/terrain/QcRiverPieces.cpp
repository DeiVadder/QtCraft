// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcRiverPieces.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <unordered_map>

namespace {

// The walk marches along the middle line in steps of marchStep blocks (bent coordinates, where
// the middle line bends only gently: round radii of a hundred blocks and more) and looks at the
// river where the line crosses a line of a lattice of `lattice` blocks: the same points from
// wherever a walk starts, so every walk along a piece sees the same ends (a lake or a dry stretch
// shorter than the lattice is seen by all walks or by none). A step starts from the last crossing
// passed, so walks heading the same way along a line step through the same points from their first
// crossing on (from the second where two crossings lie close together): where the middle line
// branches (a saddle of the noise at zero), they go the same way or lose the line at the same
// point (decision E-926). Not made to agree: a walk starting within one lattice gap before the
// saddle (it reaches it before a shared crossing), and walks coming from different arms of the
// branching.
constexpr double marchStep = 12.0;
constexpr double lattice = 24.0;
// Back within this many blocks of its first crossing, a walk has gone round a loop.
constexpr double roundAgain = 0.5;
// A point settles on the middle line within `settled` blocks of it after at most lineSteps steps
// along the noise's slope, at most lineReach blocks from where it started (a march step at most
// half a step aside: further, it jumped to another line where two come close, at a saddle of the
// noise); a crossing settles on its lattice line within `settled` blocks.
constexpr double settled = 0.01;
constexpr int lineSteps = 6;
constexpr double lineReach = 40.0;
// A river has run dry where its strength on the middle line falls below this: below it the valley
// is no longer carved fully (QcRiverField::carve()), and through higher land its water ends.
constexpr double dryStrength = 0.3;
// The answers are found from the middle of cells of this many blocks (bent coordinates; a column
// of a river finds the cell of the nearest point of its middle line); the last ones found (at most
// maxRemembered) are kept per thread.
constexpr double pieceCell = 32.0;
constexpr std::size_t maxRemembered = 1 << 14;

// Which answer a thread remembers: the pieces (each instance its own) and the cell.
struct PieceKey
{
    quint64 pieces = 0;
    qint32 cellX = 0;
    qint32 cellZ = 0;

    bool operator==(const PieceKey &) const = default;
};

struct PieceKeyHash
{
    std::size_t operator()(PieceKey key) const
    {
        const quint64 cell = (static_cast<quint64>(static_cast<quint32>(key.cellX)) << 32) | static_cast<quint32>(key.cellZ);
        return static_cast<std::size_t>(qcMixSeed(key.pieces ^ cell, 5));
    }
};

} // namespace

quint64 QcRiverPieces::nextInstance()
{
    static std::atomic<quint64> instances{0};
    return ++instances;
}

QcRiverPieces::QcRiverPieces(const QcRiverField &rivers)
    : m_instance(nextInstance())
    , m_rivers(rivers)
{
}

bool QcRiverPieces::leadsOn(const QcRiverField::Sample &sample, const QcRiverField::Land &land) const
{
    return cellLeadsOn(static_cast<qint32>(std::floor(sample.lineX / pieceCell)), static_cast<qint32>(std::floor(sample.lineZ / pieceCell)), land);
}

bool QcRiverPieces::cellLeadsOn(qint32 cellX, qint32 cellZ, const QcRiverField::Land &land) const
{
    // Every column of a cell follows the river from the cell's middle: a pure function of the
    // pieces and the cell, so the order of generation does not matter.
    const PieceKey key{m_instance, cellX, cellZ};
    thread_local std::unordered_map<PieceKey, bool, PieceKeyHash> remembered;
    if (const auto found = remembered.find(key); found != remembered.end()) {
        return found->second;
    }
    if (remembered.size() >= maxRemembered) {
        remembered.clear();
    }
    const bool leads = follow({(cellX + 0.5) * pieceCell, (cellZ + 0.5) * pieceCell}, land);
    remembered.emplace(key, leads);
    return leads;
}

bool QcRiverPieces::follow(QcRiverField::Point from, const QcRiverField::Land &land) const
{
    // One way first, then the other way for what is left of `longest`: a closed piece of up to
    // `longest` blocks ends on land both ways from each of its points, and a longer one runs out
    // of the length from each, so all its points agree. Where the steps lose the line the river
    // stays.
    const std::optional<OnLine> start = ontoLine(from, lineReach);
    if (!start) {
        return true;
    }
    const Walk ahead = walk(*start, 1.0, longest, land);
    if (ahead.end == End::Round) {
        return false;
    }
    if (ahead.end != End::Dry) {
        return true;
    }
    const Walk behind = walk(*start, -1.0, longest - ahead.length, land);
    return behind.end != End::Dry && behind.end != End::Round;
}

QcRiverPieces::Walk QcRiverPieces::walk(const OnLine &from, double side, double budget, const QcRiverField::Land &land) const
{
    OnLine here{from.at, side * from.alongX, side * from.alongZ};
    std::optional<QcRiverField::Point> first;
    std::optional<QcRiverField::Point> last;
    Walk walked;
    while (walked.length < budget) {
        const QcRiverField::Point ahead{here.at.x + marchStep * here.alongX, here.at.z + marchStep * here.alongZ};
        const std::optional<OnLine> next = ontoLine(ahead, 0.5 * marchStep);
        if (!next) {
            return {End::Lost, walked.length};
        }
        const Crossings crossed = crossings(here.at, next->at);
        std::optional<QcRiverField::Point> passed;
        for (int i = 0; i < crossed.count; ++i) {
            const QcRiverField::Point at = crossed.at[static_cast<std::size_t>(i)];
            // The length up to the crossing the walk ends at, not to the end of the step.
            const double length = walked.length + std::hypot(at.x - here.at.x, at.z - here.at.z);
            // A march point right at a lattice line finds its crossing again with the next step.
            if (last && std::hypot(at.x - last->x, at.z - last->z) < roundAgain) {
                continue;
            }
            if (first && std::hypot(at.x - first->x, at.z - first->z) < roundAgain) {
                return {End::Round, length};
            }
            first = first ? first : at;
            last = at;
            passed = at;
            if (const End end = endAt(at, land); end != End::Far) {
                return {end, length};
            }
        }
        // The next step starts from the last crossing passed: from there on the walks along the
        // line this way step through the same points, wherever they started (decision E-926).
        const QcRiverField::Point to = passed ? *passed : next->at;
        walked.length += std::hypot(to.x - here.at.x, to.z - here.at.z);
        here = along(to, here.alongX, here.alongZ);
    }
    return walked;
}

QcRiverPieces::End QcRiverPieces::endAt(QcRiverField::Point at, const QcRiverField::Land &land) const
{
    const QcRiverField::Point column = m_rivers.unbent(at.x, at.z);
    const int x = static_cast<int>(std::lround(column.x));
    const int z = static_cast<int>(std::lround(column.z));
    const std::optional<QcTerrainClimate::Point> climate = land.dryClimateAt(x, z);
    if (!climate) {
        return End::Sea;
    }
    return m_rivers.strengthOnLine(at.x, at.z, *climate) < dryStrength ? End::Dry : End::Far;
}

QcRiverPieces::OnLine QcRiverPieces::along(QcRiverField::Point at, double towardsX, double towardsZ) const
{
    // The noise's gradient turned by a right angle, the way that keeps on towards (towardsX,
    // towardsZ).
    const double here = m_rivers.value(at.x, at.z);
    const double gradX = m_rivers.value(at.x + 1.0, at.z) - here;
    const double gradZ = m_rivers.value(at.x, at.z + 1.0) - here;
    const double slope = std::max(std::hypot(gradX, gradZ), 1.0e-12);
    const double sign = -gradZ * towardsX + gradX * towardsZ < 0.0 ? -1.0 : 1.0;
    return {at, -sign * gradZ / slope, sign * gradX / slope};
}

std::optional<QcRiverPieces::OnLine> QcRiverPieces::ontoLine(QcRiverField::Point near, double reach) const
{
    // Newton steps along the noise's slope onto its zero line.
    QcRiverField::Point at = near;
    for (int step = 0; step < lineSteps; ++step) {
        const double here = m_rivers.value(at.x, at.z);
        const double gradX = m_rivers.value(at.x + 1.0, at.z) - here;
        const double gradZ = m_rivers.value(at.x, at.z + 1.0) - here;
        const double slope = std::hypot(gradX, gradZ);
        if (slope <= 0.0) {
            return std::nullopt;
        }
        if (std::abs(here) / slope < settled) {
            if (std::hypot(at.x - near.x, at.z - near.z) > reach) {
                return std::nullopt;
            }
            return OnLine{at, -gradZ / slope, gradX / slope};
        }
        at = {at.x - here * gradX / (slope * slope), at.z - here * gradZ / (slope * slope)};
    }
    return std::nullopt;
}

QcRiverPieces::Crossings QcRiverPieces::crossings(QcRiverField::Point from, QcRiverField::Point to) const
{
    // A step shorter than the lattice crosses at most one lattice line across x and one across z.
    Crossings crossed;
    std::array<double, 2> share{};
    for (const bool acrossX : {true, false}) {
        const double a = acrossX ? from.x : from.z;
        const double b = acrossX ? to.x : to.z;
        if (std::floor(a / lattice) == std::floor(b / lattice)) {
            continue;
        }
        const double line = std::max(std::floor(a / lattice), std::floor(b / lattice)) * lattice;
        const double t = (line - a) / (b - a);
        const std::optional<QcRiverField::Point> at = crossingOn(acrossX, line, {from.x + t * (to.x - from.x), from.z + t * (to.z - from.z)});
        // One that does not settle (the middle line grazing the lattice line) is left out; the walk
        // goes on.
        if (!at) {
            continue;
        }
        // In the order the walk passes them.
        const bool before = crossed.count == 1 && t < share[0];
        crossed.at[static_cast<std::size_t>(crossed.count)] = *at;
        share[static_cast<std::size_t>(crossed.count)] = t;
        if (before) {
            std::swap(crossed.at[0], crossed.at[1]);
        }
        ++crossed.count;
    }
    return crossed;
}

std::optional<QcRiverField::Point> QcRiverPieces::crossingOn(bool acrossX, double line, QcRiverField::Point guess) const
{
    // Newton steps along the lattice line onto the middle line; the guess lies on the chord of a
    // march step, a fraction of a block from the line.
    QcRiverField::Point at = guess;
    // Exactly on the lattice line, so walks from anywhere find the very same point.
    (acrossX ? at.x : at.z) = line;
    for (int step = 0; step < lineSteps; ++step) {
        const double here = m_rivers.value(at.x, at.z);
        const double slope = acrossX ? m_rivers.value(at.x, at.z + 1.0) - here : m_rivers.value(at.x + 1.0, at.z) - here;
        if (slope == 0.0) {
            return std::nullopt;
        }
        const double move = std::clamp(here / slope, -0.5 * marchStep, 0.5 * marchStep);
        if (std::abs(move) < settled) {
            return std::hypot(at.x - guess.x, at.z - guess.z) <= marchStep ? std::optional(at) : std::nullopt;
        }
        (acrossX ? at.z : at.x) -= move;
    }
    return std::nullopt;
}
