// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcRaycast.h"

#include "common/content/registry/QcContentRegistry.h"

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace QcRaycast {

[[nodiscard]] std::optional<double> intersectBox(const QcVec3 &origin, const QcVec3 &direction, const QcAabb &box, double maxDistance)
{
    // Slab method: intersect the ray's entry and exit distances over the three axes.
    double entry = 0.0;
    double exit = maxDistance;
    const double origins[3] = {origin.x, origin.y, origin.z};
    const double directions[3] = {direction.x, direction.y, direction.z};
    const double lows[3] = {box.min.x, box.min.y, box.min.z};
    const double highs[3] = {box.max.x, box.max.y, box.max.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (directions[axis] == 0.0) {
            if (origins[axis] < lows[axis] || origins[axis] > highs[axis]) {
                return std::nullopt;
            }
            continue;
        }

        double enter = (lows[axis] - origins[axis]) / directions[axis];
        double leave = (highs[axis] - origins[axis]) / directions[axis];
        if (enter > leave) {
            std::swap(enter, leave);
        }
        entry = std::max(entry, enter);
        exit = std::min(exit, leave);
        if (entry > exit) {
            return std::nullopt;
        }
    }
    return entry;
}

[[nodiscard]] std::optional<std::pair<double, QcBlockFace>> intersectBoxFace(const QcVec3 &origin, const QcVec3 &direction, const QcAabb &box,
                                                               double maxDistance, QcBlockFace insideFace)
{
    double entry = 0.0;
    double exit = maxDistance;
    QcBlockFace face = insideFace;
    const double origins[3] = {origin.x, origin.y, origin.z};
    const double directions[3] = {direction.x, direction.y, direction.z};
    const double lows[3] = {box.min.x, box.min.y, box.min.z};
    const double highs[3] = {box.max.x, box.max.y, box.max.z};
    // Entering through the low side of an axis means hitting the face that points to negative.
    constexpr std::array<std::pair<QcBlockFace, QcBlockFace>, 3> faces = {
        {{QcBlockFace::West, QcBlockFace::East}, {QcBlockFace::Down, QcBlockFace::Up}, {QcBlockFace::North, QcBlockFace::South}}};

    for (int axis = 0; axis < 3; ++axis) {
        if (directions[axis] == 0.0) {
            if (origins[axis] < lows[axis] || origins[axis] > highs[axis]) {
                return std::nullopt;
            }
            continue;
        }

        const bool positive = directions[axis] > 0.0;
        const double enter = ((positive ? lows[axis] : highs[axis]) - origins[axis]) / directions[axis];
        const double leave = ((positive ? highs[axis] : lows[axis]) - origins[axis]) / directions[axis];
        if (enter > entry) {
            entry = enter;
            face = positive ? faces[static_cast<std::size_t>(axis)].first : faces[static_cast<std::size_t>(axis)].second;
        }
        exit = std::min(exit, leave);
        if (entry > exit) {
            return std::nullopt;
        }
    }
    return std::pair{entry, face};
}

namespace {

// A ray being cast: normalised direction, length limit and what it may hit.
struct Ray
{
    QcVec3 origin;
    QcVec3 dir;
    double maxDistance = 0.0;
    Targets targets = Targets::Visible;
};

using FaceHit = std::optional<std::pair<double, QcBlockFace>>;

// Voxel traversal state: the current cell and, per axis, the step direction, the distance between
// two grid planes and the distance to the next plane crossing.
struct Traversal
{
    QcBlockPos cell;
    int stepX = 0;
    int stepY = 0;
    int stepZ = 0;
    double deltaX = 0.0;
    double deltaY = 0.0;
    double deltaZ = 0.0;
    double nextX = 0.0;
    double nextY = 0.0;
    double nextZ = 0.0;
    double travelled = 0.0;
    // Face through which the ray entered the current cell.
    QcBlockFace face = QcBlockFace::Up;

    // Moves to the next cell along the ray.
    void advance()
    {
        if (nextX < nextY && nextX < nextZ) {
            cell.x += stepX;
            travelled = nextX;
            nextX += deltaX;
            face = stepX > 0 ? QcBlockFace::West : QcBlockFace::East;
        } else if (nextY < nextZ) {
            cell.y += stepY;
            travelled = nextY;
            nextY += deltaY;
            face = stepY > 0 ? QcBlockFace::Down : QcBlockFace::Up;
        } else {
            cell.z += stepZ;
            travelled = nextZ;
            nextZ += deltaZ;
            face = stepZ > 0 ? QcBlockFace::North : QcBlockFace::South;
        }
    }
};

Traversal startTraversal(const QcVec3 &origin, const QcVec3 &dir)
{
    Traversal t;
    t.cell = {static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)), static_cast<int>(std::floor(origin.z))};
    t.stepX = dir.x > 0 ? 1 : (dir.x < 0 ? -1 : 0);
    t.stepY = dir.y > 0 ? 1 : (dir.y < 0 ? -1 : 0);
    t.stepZ = dir.z > 0 ? 1 : (dir.z < 0 ? -1 : 0);

    constexpr double infinity = std::numeric_limits<double>::infinity();
    t.deltaX = t.stepX != 0 ? std::abs(1.0 / dir.x) : infinity;
    t.deltaY = t.stepY != 0 ? std::abs(1.0 / dir.y) : infinity;
    t.deltaZ = t.stepZ != 0 ? std::abs(1.0 / dir.z) : infinity;

    t.nextX = t.stepX > 0 ? (t.cell.x + 1 - origin.x) * t.deltaX : (t.stepX < 0 ? (origin.x - t.cell.x) * t.deltaX : infinity);
    t.nextY = t.stepY > 0 ? (t.cell.y + 1 - origin.y) * t.deltaY : (t.stepY < 0 ? (origin.y - t.cell.y) * t.deltaY : infinity);
    t.nextZ = t.stepZ > 0 ? (t.cell.z + 1 - origin.z) * t.deltaZ : (t.stepZ < 0 ? (origin.z - t.cell.z) * t.deltaZ : infinity);
    return t;
}

bool isTarget(const QcBlockDefinition &definition, Targets targets)
{
    const bool visible = definition.renderLayer != QcRenderLayer::Invisible;
    return targets == Targets::Solid             ? definition.solid
           : targets == Targets::VisibleAndLiquids ? visible || definition.liquid
                                                 : !definition.liquid && visible;
}

// The nearest of the block-local `boxes` of the block at `cell` that the ray hits.
FaceHit nearestHit(const Ray &ray, const QList<QcAabb> &boxes, QcBlockPos cell, QcBlockFace face)
{
    const QcVec3 corner{static_cast<double>(cell.x), static_cast<double>(cell.y), static_cast<double>(cell.z)};
    FaceHit nearest;
    for (const QcAabb &box : boxes) {
        const auto hit = intersectBoxFace(ray.origin, ray.dir, box.moved(corner), ray.maxDistance, face);
        if (hit && (!nearest || hit->first < nearest->first)) {
            nearest = hit;
        }
    }
    return nearest;
}

// The hit on `block` in `cell`: the nearest box of its state; a ray that passes between its boxes goes on.
[[nodiscard]] std::optional<QcRaycastHit> hitBlock(const QcContentRegistry &content, const Ray &ray, QcBlockPos cell, QcBlockId block,
                                     QcBlockFace face)
{
    if (!content.isValidBlock(block) || block == qcAirBlockId || !isTarget(content.block(block), ray.targets)) {
        return std::nullopt;
    }

    const QcBlockState &state = content.state(block);
    const FaceHit nearest = nearestHit(ray, ray.targets == Targets::Solid ? state.collision : state.targetBoxes(), cell, face);
    if (!nearest) {
        return std::nullopt;
    }
    return QcRaycastHit{cell, nearest->second, nearest->first, ray.origin + ray.dir * nearest->first};
}

// Collision boxes reach up to 1.5 m (fences): the part above the block below `cell` lies in this cell.
[[nodiscard]] std::optional<QcRaycastHit> hitUpperPartBelow(const QcBlockAccess &world, const QcContentRegistry &content, const Ray &ray,
                                              QcBlockPos cell, QcBlockFace face)
{
    if (ray.targets != Targets::Solid || cell.y <= 0) {
        return std::nullopt;
    }

    const QcBlockPos belowPos = cell.offset(0, -1, 0);
    const std::optional<QcBlockId> below = world.blockAt(belowPos);
    if (!below || !content.isValidBlock(*below) || !content.block(*below).solid) {
        return std::nullopt;
    }

    const QcVec3 corner{static_cast<double>(belowPos.x), static_cast<double>(belowPos.y), static_cast<double>(belowPos.z)};
    FaceHit nearest;
    for (const QcAabb &box : content.state(*below).collision) {
        if (box.max.y <= 1.0) {
            continue;
        }

        const QcAabb upper{{box.min.x, std::max(box.min.y, 1.0), box.min.z}, box.max};
        const auto hit = intersectBoxFace(ray.origin, ray.dir, upper.moved(corner), ray.maxDistance, face);
        if (hit && (!nearest || hit->first < nearest->first)) {
            nearest = hit;
        }
    }

    if (!nearest) {
        return std::nullopt;
    }
    return QcRaycastHit{belowPos, nearest->second, nearest->first, ray.origin + ray.dir * nearest->first};
}

} // namespace

[[nodiscard]] std::optional<QcRaycastHit> cast(const QcBlockAccess &world, const QcVec3 &origin, const QcVec3 &direction, double maxDistance,
                                 Targets targets)
{
    const QcVec3 dir = direction.normalized();
    if (dir.length() == 0.0) {
        return std::nullopt;
    }

    const Ray ray{origin, dir, maxDistance, targets};
    Traversal t = startTraversal(origin, dir);
    const QcContentRegistry &content = world.content();

    while (t.travelled <= maxDistance) {
        if (t.cell.y >= 0 && t.cell.y < QcWorld::worldHeight) {
            const std::optional<QcBlockId> block = world.blockAt(t.cell);
            if (!block) {
                return std::nullopt;
            }

            if (std::optional<QcRaycastHit> hit = hitBlock(content, ray, t.cell, *block, t.face)) {
                return hit;
            }
            if (std::optional<QcRaycastHit> hit = hitUpperPartBelow(world, content, ray, t.cell, t.face)) {
                return hit;
            }
        }
        t.advance();
    }
    return std::nullopt;
}

} // namespace QcRaycast
