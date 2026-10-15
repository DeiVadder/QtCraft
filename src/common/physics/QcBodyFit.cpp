#include "QcBodyFit.h"

#include "QcContentRegistry.h"

#include <array>
#include <cmath>

namespace QcBodyFit {

namespace {

// Keeps a body that only touches a block face from counting as inside it.
constexpr double inset = 1e-6;

bool cellOverlaps(const QcBlockAccess &world, QcBlockPos cell, const QcAabb &box, Unloaded unloaded)
{
    if (cell.y < 0) {
        return false;
    }

    const QcVec3 origin{double(cell.x), double(cell.y), double(cell.z)};
    const std::optional<QcBlockId> block = world.blockAt(cell);
    if (!block) {
        return unloaded == Unloaded::Blocked && QcAabb{origin, origin + QcVec3{1.0, 1.0, 1.0}}.intersects(box);
    }

    // The state's own boxes, without collecting them: air and plants have none.
    const QcContentRegistry &content = world.content();
    if (!content.isValidBlock(*block)) {
        return false;
    }
    for (const QcAabb &solid : content.state(*block).collision) {
        if (solid.moved(origin).intersects(box)) {
            return true;
        }
    }
    return false;
}

// Whether a cell from `start` to z = `lastZ` has a collision box overlapping `box`.
bool rowOverlaps(const QcBlockAccess &world, QcBlockPos start, int lastZ, const QcAabb &box, Unloaded unloaded)
{
    for (QcBlockPos cell = start; cell.z <= lastZ; ++cell.z) {
        if (cellOverlaps(world, cell, box, unloaded)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool overlapsBlocks(const QcBlockAccess &world, const QcAabb &box, Unloaded unloaded)
{
    const QcVec3 margin{inset, inset, inset};
    const QcAabb inner{box.min + margin, box.max - margin};
    const QcBlockPos low = QcPhysics::blockContaining(inner.min);
    const QcBlockPos high = QcPhysics::blockContaining(inner.max);
    // One cell lower: collision boxes reach up to 1.5 m into the cell above.
    for (int x = low.x; x <= high.x; ++x) {
        for (int y = low.y - 1; y <= high.y; ++y) {
            if (rowOverlaps(world, {x, y, low.z}, high.z, inner, unloaded)) {
                return true;
            }
        }
    }
    return false;
}

bool overlapsBlocks(const QcBlockAccess &world, const QcVec3 &feet, QcPhysics::BodySize size, Unloaded unloaded)
{
    return overlapsBlocks(world, QcPhysics::boxAt(feet, size.width, size.height), unloaded);
}

[[nodiscard]] std::optional<QcVec3> clearFeet(const QcBlockAccess &world, const QcVec3 &feet, QcPhysics::BodySize size)
{
    if (!overlapsBlocks(world, feet, size, Unloaded::Clear)) {
        return feet;
    }

    // Up stands the body on the top face of the block its feet are in; only one metre, so a body
    // under a one-block ceiling does not end up on the roof.
    const std::array<QcVec3, 6> candidates{{
        {feet.x, std::floor(feet.y) + 1.0, feet.z},
        feet + QcVec3{0.0, 0.0, -1.0},
        feet + QcVec3{1.0, 0.0, 0.0},
        feet + QcVec3{0.0, 0.0, 1.0},
        feet + QcVec3{-1.0, 0.0, 0.0},
        feet + QcVec3{0.0, -1.0, 0.0},
    }};
    for (const QcVec3 &candidate : candidates) {
        if (candidate.y >= 0.0 && !overlapsBlocks(world, candidate, size, Unloaded::Blocked)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace QcBodyFit
