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
// wherever a walk starts (solved on the lattice line to `settled` blocks and snapped to 1/`snap`
// of a block), so walks along a piece see the same ends (a lake shorter than the lattice is seen
// by all walks or by none). Both walks of a cell start at the first crossing ahead of the cell's
// point of the line, and a step starts from the last crossing passed, so walks heading the same
// way along a line step through the same points (from the second crossing on where two crossings
// lie close together): where the middle line branches (a saddle of the noise at zero), they go the
// same way or lose the line at the same point (decision E-926). Not made to agree: a walk starting
// within one lattice gap before the saddle (it reaches it before a shared crossing), walks coming
// from different arms of the branching, two crossings of a lattice corner closer than roundAgain
// (a walk takes the second one only) and a crossing whose snapped coordinate lies at a rounding
// boundary. Between two crossings passed one after the other the river is also read every dryStep
// blocks or less on the chord between them (decision E-935), at points that depend only on the
// two crossings: a stretch of a river that the fading of small rings dries out over a few blocks
// (QcRiverField::ringsLeaveOnLine(), from tops found per cell of 8 blocks at a tight bend) breaks
// the water there, and the crossings alone can lie on both sides of it.
constexpr double marchStep = 12.0;
constexpr double lattice = 24.0;
constexpr double dryStep = 6.0;
constexpr double snap = 64.0;
// The first crossing ahead of a cell's point of the line lies within this many march steps (four
// lattice gaps).
constexpr int firstSteps = 8;
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
// The answers of the last cells found (at most maxRemembered) are kept per thread.
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
    return cellLeadsOn(static_cast<qint32>(std::floor(sample.lineX / cellSize)), static_cast<qint32>(std::floor(sample.lineZ / cellSize)), land);
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
    const bool leads = follow({(cellX + 0.5) * cellSize, (cellZ + 0.5) * cellSize}, land);
    remembered.emplace(key, leads);
    return leads;
}

bool QcRiverPieces::follow(QcRiverField::Point from, const QcRiverField::Land &land) const
{
    // One way first, then the other way for what is left of `longest`: a closed piece of up to
    // `longest` blocks ends on land both ways from each of its points, and a longer one runs out
    // of the length from each, so all its points agree. Where the steps lose the line the river
    // stays.
    const std::optional<Start> start = startNear(from, land);
    if (!start) {
        return true;
    }
    const Walk ahead = walkFrom(*start, 1.0, longest, land);
    if (ahead.end == End::Round) {
        return false;
    }
    if (ahead.end != End::Dry) {
        return true;
    }
    const Walk behind = walkFrom(*start, -1.0, longest - ahead.length, land);
    return behind.end != End::Dry && behind.end != End::Round;
}

std::optional<QcRiverPieces::Walk> QcRiverPieces::walkFrom(QcRiverField::Point near, double side, const QcRiverField::Land &land) const
{
    const std::optional<Start> start = startNear(near, land);
    return start ? std::optional(walkFrom(*start, side, longest, land)) : std::nullopt;
}

std::optional<QcRiverPieces::Start> QcRiverPieces::startNear(QcRiverField::Point near, const QcRiverField::Land &land) const
{
    const std::optional<OnLine> onLine = ontoLine(near, lineReach);
    if (!onLine) {
        return std::nullopt;
    }
    // Marching on to the first crossing ahead, as a walk does.
    OnLine here = *onLine;
    for (int step = 0; step < firstSteps; ++step) {
        const QcRiverField::Point ahead{here.at.x + marchStep * here.alongX, here.at.z + marchStep * here.alongZ};
        const std::optional<OnLine> next = ontoLine(ahead, 0.5 * marchStep);
        if (!next) {
            return std::nullopt;
        }
        if (const Crossings crossed = crossings(here.at, next->at); crossed.count > 0) {
            return Start{along(crossed.at[0], here.alongX, here.alongZ), seenAt(crossed.at[0], land)};
        }
        here = along(next->at, here.alongX, here.alongZ);
    }
    return std::nullopt;
}

QcRiverPieces::Walk QcRiverPieces::walkFrom(const Start &start, double side, double budget, const QcRiverField::Land &land) const
{
    // The first crossing lies ahead: where the river ends there, it ends right away that way.
    if (side > 0.0 && start.seen.end != End::Far) {
        return {start.seen.end, 0.0};
    }
    const OnLine from{start.at.at, side * start.at.alongX, side * start.at.alongZ};
    // A crossing in the sea has no strength to read the stretch behind it with.
    const std::optional<Passed> last = start.seen.end == End::Sea ? std::nullopt : std::optional(start.seen.passed);
    return walk(from, budget, {start.at.at, last}, land);
}

QcRiverPieces::Walk QcRiverPieces::walk(const OnLine &from, double budget, Trail trail, const QcRiverField::Land &land) const
{
    OnLine here = from;
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
            const Passing passing = pass(trail, at, walked.length + std::hypot(at.x - here.at.x, at.z - here.at.z), land);
            if (passing.ended) {
                return *passing.ended;
            }
            if (passing.passed) {
                trail.first = trail.first ? trail.first : at;
                trail.last = passing.passed;
                passed = at;
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

QcRiverPieces::Passing QcRiverPieces::pass(const Trail &trail, QcRiverField::Point at, double length, const QcRiverField::Land &land) const
{
    // A march point right at a lattice line finds its crossing again with the next step.
    if (trail.last && std::hypot(at.x - trail.last->at.x, at.z - trail.last->at.z) < roundAgain) {
        return {};
    }
    if (trail.first && std::hypot(at.x - trail.first->x, at.z - trail.first->z) < roundAgain) {
        return {Walk{End::Round, length}, std::nullopt};
    }
    const Seen seen = seenAt(at, land);
    if (trail.last) {
        // In the sea the climate of the crossing before counts on to it.
        const Passed reached{at, seen.end == End::Sea ? trail.last->strength : seen.passed.strength};
        if (const std::optional<QcRiverField::Point> dry = dryBetween(*trail.last, reached)) {
            return {Walk{End::Dry, length - std::hypot(at.x - dry->x, at.z - dry->z)}, std::nullopt};
        }
    }
    if (seen.end != End::Far) {
        return {Walk{seen.end, length}, std::nullopt};
    }
    return {std::nullopt, seen.passed};
}

QcRiverPieces::Seen QcRiverPieces::seenAt(QcRiverField::Point at, const QcRiverField::Land &land) const
{
    const QcRiverField::Point column = m_rivers.unbent(at.x, at.z);
    const int x = static_cast<int>(std::lround(column.x));
    const int z = static_cast<int>(std::lround(column.z));
    const std::optional<QcTerrainClimate::Point> climate = land.dryClimateAt(x, z);
    if (!climate) {
        return {End::Sea, {at, 0.0}};
    }
    const double strength = QcRiverField::strengthAt(*climate);
    const bool dry = strength < dryStrength || strength * m_rivers.ringsLeaveOnLine(at.x, at.z) < dryStrength;
    return {dry ? End::Dry : End::Far, {at, strength}};
}

std::optional<QcRiverField::Point> QcRiverPieces::dryBetween(const Passed &one, const Passed &other) const
{
    // The points lie on the chord between the crossings (the line bends gently: a fraction of a
    // block off it over a lattice gap) at equal shares of it from the crossing first in x, then z,
    // so walks either way read the same points; the climate's strength is taken between the
    // crossings' (it changes over hundreds of blocks).
    const bool oneFirst = one.at.x < other.at.x || (one.at.x == other.at.x && one.at.z < other.at.z);
    const Passed &from = oneFirst ? one : other;
    const Passed &to = oneFirst ? other : one;
    const int parts = static_cast<int>(std::ceil(std::hypot(to.at.x - from.at.x, to.at.z - from.at.z) / dryStep));
    for (int part = 1; part < parts; ++part) {
        const double share = static_cast<double>(part) / parts;
        const QcRiverField::Point at{from.at.x + share * (to.at.x - from.at.x), from.at.z + share * (to.at.z - from.at.z)};
        const double strength = from.strength + share * (to.strength - from.strength);
        if (strength < dryStrength || strength * m_rivers.ringsLeaveOnLine(at.x, at.z) < dryStrength) {
            return at;
        }
    }
    return std::nullopt;
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
    // How far along the step each crossing lies (times the step's length).
    std::array<double, 2> along{};
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
        // In the order the walk passes them: by the crossing's projection onto the step. Near
        // a corner of the lattice the curved line can pass the two lines the other way round than
        // the chord does; taking the later one first, a walk would find the earlier one again with
        // its next step and see a loop (decision E-935).
        const double ahead = (at->x - from.x) * (to.x - from.x) + (at->z - from.z) * (to.z - from.z);
        // One behind the step's start was passed before it (the crossing it starts from, or the
        // other of two near a corner).
        if (ahead <= 0.0) {
            continue;
        }
        const bool before = crossed.count == 1 && ahead < along[0];
        crossed.at[static_cast<std::size_t>(crossed.count)] = *at;
        along[static_cast<std::size_t>(crossed.count)] = ahead;
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
            // Snapped, so walks from anywhere that settle on it find the very same point.
            double &free = acrossX ? at.z : at.x;
            free = std::round(free * snap) / snap;
            return std::hypot(at.x - guess.x, at.z - guess.z) <= marchStep ? std::optional(at) : std::nullopt;
        }
        (acrossX ? at.z : at.x) -= move;
    }
    return std::nullopt;
}
