// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/bastion/QcBastionSketch.h"

#include <algorithm>
#include <limits>

namespace {

using Plan = QcBastionPlan;
using Side = QcBastionPlan::Side;
using Paint = QcBastionPlan::Paint;

// The cells over a landing's deck that are emptied, half its width, and how long it gets at most.
constexpr int landingHeight = 4;
constexpr int landingHalf = 2;
constexpr int longestLanding = 24;
// The free cells kept over a step.
constexpr int stepRoom = 3;

// `side` a quarter turn clockwise `turns` times.
Side turned(Side side, int turns)
{
    return static_cast<Side>((static_cast<int>(side) + turns) % 4);
}

} // namespace

QcBastionSketch::QcBastionSketch(QcBastionPlan &plan, int turns, Roll roll, IsRock isRock)
    : m_plan(plan)
    , m_turns(turns)
    , m_roll(std::move(roll))
    , m_isRock(std::move(isRock))
{
}

QcBlockPos QcBastionSketch::world(QcBlockPos local) const
{
    int x = local.x;
    int z = local.z;
    for (int turn = 0; turn < m_turns; ++turn) {
        const int oldX = x;
        x = -z;
        z = oldX;
    }
    return m_plan.centre.offset(x, local.y, z);
}

QcBlockBox QcBastionSketch::world(const QcBlockBox &box) const
{
    const QcBlockPos a = world(box.low);
    const QcBlockPos b = world(box.high);
    return {{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}, {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}};
}

Side QcBastionSketch::world(Side side) const
{
    return turned(side, m_turns);
}

void QcBastionSketch::paint(const QcBlockBox &box, Paint paint, Side side)
{
    plan().strokes.append({world(box), paint, world(side)});
}

void QcBastionSketch::addPart(const QcBlockBox &box)
{
    const QcBlockBox cells = world(box);
    plan().parts.append({cells.low.offset(-1, -1, -1), cells.high.offset(1, 1, 1)});
}

void QcBastionSketch::hall(const QcBlockBox &box)
{
    const QcBlockPos low = box.low;
    const QcBlockPos high = box.high;
    paint({low.offset(0, -1, 0), {high.x, low.y - 1, high.z}}, Paint::Foundation);
    paint(box, Paint::Brick);
    paint({low.offset(1, 1, 1), high.offset(-1, -1, -1)}, Paint::Air);
    addPart(box);
}

void QcBastionSketch::yard(const QcBlockBox &box, int thick)
{
    const QcBlockPos low = box.low;
    const QcBlockPos high = box.high;
    paint({low.offset(0, -1, 0), {high.x, low.y - 1, high.z}}, Paint::Foundation);
    paint({low, {high.x, low.y, high.z}}, Paint::Rough);
    paint({low.offset(0, 1, 0), high}, Paint::Brick);
    paint({low.offset(thick, 1, thick), high.offset(-thick, 4, -thick)}, Paint::Air);
    addPart(box);
}

void QcBastionSketch::deck(const QcBlockBox &box, Side along)
{
    const QcBlockPos low = box.low;
    const QcBlockPos high = box.high;
    const bool alongX = along == Side::East || along == Side::West;
    paint({low, {high.x, low.y, high.z}}, Paint::Brick);
    paint({low.offset(0, 1, 0), high}, Paint::Air);
    for (const bool first : {true, false}) {
        const QcBlockPos from = first ? low.offset(0, 1, 0) : (alongX ? QcBlockPos{low.x, low.y + 1, high.z} : QcBlockPos{high.x, low.y + 1, low.z});
        const QcBlockPos to = alongX ? QcBlockPos{high.x, from.y, from.z} : QcBlockPos{from.x, from.y, high.z};
        paint({from, to}, Paint::Wall, along);
    }
    addPart(box);
}

void QcBastionSketch::door(QcBlockPos at, Side outward, QcBastionDoorway size)
{
    const QcBlockPos near = QcFortressPlan::onSide(at, outward, 0, -size.half);
    const QcBlockPos far = QcFortressPlan::onSide(at, outward, 1 - size.thick, size.half);
    paint({near, far.offset(0, size.height - 1, 0)}, Paint::Air);
}

void QcBastionSketch::flight(QcBlockPos from, Side side, int rise, int width)
{
    for (int step = 0; step < rise; ++step) {
        const QcBlockPos near = QcFortressPlan::onSide(from, side, step, 0);
        const QcBlockPos far = QcFortressPlan::onSide(near, side, 0, width - 1);
        if (step > 0) {
            paint({near.offset(0, 1, 0), far.offset(0, step, 0)}, Paint::Brick);
        }
        paint({near.offset(0, step + 1, 0), far.offset(0, step + 1, 0)}, Paint::Stairs, side);
        paint({near.offset(0, step + 2, 0), far.offset(0, step + 1 + stepRoom, 0)}, Paint::Air);
    }
}

void QcBastionSketch::landing(QcBlockPos at, Side side)
{
    plan().entrance = world(QcFortressPlan::onSide(at, side, 1, 0)).offset(0, 1, 0);
    int length = 1;
    while (length < longestLanding) {
        const QcBlockPos next = world(QcFortressPlan::onSide(at, side, length, 0));
        const bool ground = isRock(next);
        const bool free = !isRock(next.offset(0, 1, 0)) && !isRock(next.offset(0, 2, 0));
        if (ground && free) {
            break;
        }
        ++length;
    }
    const QcBlockPos near = QcFortressPlan::onSide(at, side, 1, -landingHalf);
    const QcBlockPos far = QcFortressPlan::onSide(at, side, length, landingHalf);
    paint({near.offset(0, -1, 0), far.offset(0, -1, 0)}, Paint::Foundation);
    paint({near, far}, Paint::Rough);
    paint({near.offset(0, 1, 0), far.offset(0, landingHeight, 0)}, Paint::Air);
}

void QcBastionSketch::merlons(const QcBlockBox &box)
{
    const QcBlockPos low = box.low;
    const QcBlockPos high = box.high;
    const int y = high.y + 1;
    for (int x = low.x; x <= high.x; x += 2) {
        paint({{x, y, low.z}, {x, y, low.z}}, Paint::Brick);
        paint({{x, y, high.z}, {x, y, high.z}}, Paint::Brick);
    }
    for (int z = low.z; z <= high.z; z += 2) {
        paint({{low.x, y, z}, {low.x, y, z}}, Paint::Brick);
        paint({{high.x, y, z}, {high.x, y, z}}, Paint::Brick);
    }
}

void QcBastionSketch::ruin(const QcBlockBox &box)
{
    paint(box, Paint::Crumble);
}

void QcBastionSketch::chest(QcBlockPos at, Side facing, Loot loot)
{
    plan().chests.append({world(at), world(facing), loot});
}

void QcBastionSketch::spawner(QcBlockPos at)
{
    plan().spawners.append(world(at));
}

void QcBastionSketch::resident(QcBlockPos at, Folk folk)
{
    plan().residents.append({world(at), folk});
}

void QcBastionSketch::sign(QcBlockPos at, const QString &text, bool heading)
{
    plan().signs.append({world(at), text, heading});
}

void QcBastionSketch::finish()
{
    QcXzRect bounds{std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
    for (const Plan::Stroke &stroke : std::as_const(plan().strokes)) {
        bounds.minX = std::min(bounds.minX, stroke.box.low.x);
        bounds.minZ = std::min(bounds.minZ, stroke.box.low.z);
        bounds.maxX = std::max(bounds.maxX, stroke.box.high.x);
        bounds.maxZ = std::max(bounds.maxZ, stroke.box.high.z);
    }
    // The seals lie one cell out.
    plan().bounds = bounds.grown(2);
}
