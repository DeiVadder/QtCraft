// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/pathing/QcSwimSpace.h"

#include "common/physics/QcBoatMotion.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace QcSwimSpace {

namespace {

// Keeps a box that ends exactly on a cell's face from counting the cell beyond it.
constexpr double faceTolerance = 1e-6;

// Whether the cells from `bottom` up to the row `top` all hold water, and a body reaching up
// to `reach` stays under the surface of the top one (a waterfall has none to keep to).
bool columnHolds(const QcBlockAccess &world, QcBlockPos bottom, int top, double reach)
{
    for (int y = bottom.y; y <= top; ++y) {
        if (!QcPhysics::isWaterAt(world, {bottom.x, y, bottom.z})) {
            return false;
        }
    }
    const std::optional<double> surface = QcBoatMotion::waterSurfaceAt(world, {bottom.x, top, bottom.z});
    return !surface || reach <= *surface;
}

} // namespace

QcVec3 pointIn(QcBlockPos cell, QcPhysics::BodySize body)
{
    const double lift = std::max(edgeMargin, 0.5 - body.height * 0.5);
    return {cell.x + 0.5, cell.y + lift, cell.z + 0.5};
}

bool fits(const QcBlockAccess &world, const QcVec3 &feet, QcPhysics::BodySize body)
{
    const QcAabb box = QcPhysics::boxAt(feet, body.width, body.height);
    const QcVec3 inset{faceTolerance, faceTolerance, faceTolerance};
    const QcBlockPos low = QcPhysics::blockContaining(box.min + inset);
    const QcBlockPos high = QcPhysics::blockContaining(box.max - inset);
    for (int x = low.x; x <= high.x; ++x) {
        for (int z = low.z; z <= high.z; ++z) {
            if (!columnHolds(world, {x, low.y, z}, high.y, box.max.y)) {
                return false;
            }
        }
    }
    return true;
}

bool clearLine(const QcBlockAccess &world, const QcVec3 &from, const QcVec3 &to, QcPhysics::BodySize body)
{
    const QcVec3 along = to - from;
    const int steps = std::max(1, static_cast<int>(std::ceil(along.length() / lineStep)));
    for (int i = 1; i <= steps; ++i) {
        if (!fits(world, from + along * (static_cast<double>(i) / steps), body)) {
            return false;
        }
    }
    return true;
}

QcVec3 keepInside(const QcVec3 &move, const QcVec3 &feet, QcPhysics::BodySize body, const QcBlockAccess &world, double reach)
{
    // Axis by axis: a wall ahead stops only the component that leads into it.
    static constexpr QcVec3 axes[] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    QcVec3 kept = move;
    for (const QcVec3 &axis : axes) {
        const double component = kept.dot(axis);
        if (component == 0.0) {
            continue;
        }
        const double distance = std::abs(component) * reach + edgeMargin;
        if (!fits(world, feet + axis * (component > 0.0 ? distance : -distance), body)) {
            kept = kept - axis * component;
        }
    }

    // Each component alone fits, the move as a whole may still cut a corner out of the water.
    const double length = kept.length();
    if (length == 0.0 || fits(world, feet + kept * reach + kept * (edgeMargin / length), body)) {
        return kept;
    }
    QcVec3 largest;
    double largestSize = 0.0;
    for (const QcVec3 &axis : axes) {
        if (const double component = kept.dot(axis); std::abs(component) > largestSize) {
            largest = axis * component;
            largestSize = std::abs(component);
        }
    }
    return largest;
}

} // namespace QcSwimSpace
