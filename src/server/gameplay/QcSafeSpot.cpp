#include "server/gameplay/QcSafeSpot.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"

#include <array>
#include <cmath>

namespace {

// What a block is to a body that comes to it: none (no chunk), open, a floor, or neither.
enum class Cell : quint8 { Unloaded, Open, Floor, Blocked };

Cell cellAt(const QcBlockAccess &world, QcBlockPos pos)
{
    if (pos.y < 0 || pos.y >= QcWorld::worldHeight) {
        return pos.y < 0 ? Cell::Blocked : Cell::Open;
    }
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    if (!block || !content.isValidBlock(*block)) {
        return Cell::Unloaded;
    }

    const QcBlockDefinition &definition = content.block(*block);
    if (definition.liquid || definition.contactDamage > 0.0 || definition.fire.setsAlightSeconds > 0.0) {
        return Cell::Blocked;
    }
    if (!definition.solid) {
        return Cell::Open;
    }
    return content.state(*block).fullCube ? Cell::Floor : Cell::Blocked;
}

QcVec3 middleOf(QcBlockPos feet)
{
    return {feet.x + 0.5, static_cast<double>(feet.y), feet.z + 0.5};
}

// The open cell a point arrived through: its own cell when open, else the first open cell back along
// the flight (a pearl on a floor lies in the floor's top); none within QcSafeSpot::backtrack.
std::optional<QcBlockPos> openCellBefore(const QcBlockAccess &world, const QcVec3 &point, const QcVec3 &along)
{
    constexpr double step = 0.05;
    const QcVec3 back = along.length() > 1e-9 ? along.normalized() * -step : QcVec3{};
    const int steps = back.length() > 0.0 ? static_cast<int>(QcSafeSpot::backtrack / step) : 0;
    for (int i = 0; i <= steps; ++i) {
        const QcBlockPos cell = QcPhysics::blockContaining(point + back * static_cast<double>(i));
        if (cellAt(world, cell) == Cell::Open) {
            return cell;
        }
    }
    return std::nullopt;
}

} // namespace

namespace QcSafeSpot {

int cellsOf(double height)
{
    return std::max(1, static_cast<int>(std::ceil(height - 1e-6)));
}

bool fits(const QcBlockAccess &world, QcBlockPos feet, int cells)
{
    for (int up = 0; up < cells; ++up) {
        if (cellAt(world, feet.offset(0, up, 0)) != Cell::Open) {
            return false;
        }
    }
    return true;
}

bool canStand(const QcBlockAccess &world, QcBlockPos feet, int cells)
{
    return cellAt(world, feet.offset(0, -1, 0)) == Cell::Floor && fits(world, feet, cells);
}

std::optional<QcVec3> randomAround(const QcBlockAccess &world, const QcVec3 &around, Search search, QRandomGenerator &random)
{
    const QcBlockPos centre = QcPhysics::blockContaining(around);
    const int reach = std::max(1, static_cast<int>(search.range));
    for (int attempt = 0; attempt < search.attempts; ++attempt) {
        const int dx = random.bounded(-reach, reach + 1);
        const int dy = random.bounded(-reach, reach + 1);
        const int dz = random.bounded(-reach, reach + 1);
        QcBlockPos feet{centre.x + dx, std::clamp(centre.y + dy, 1, QcWorld::worldHeight - search.cells), centre.z + dz};

        // Down to the first floor; a cell of a chunk that is not loaded ends the attempt.
        for (int down = 0; down <= 2 * reach && feet.y > 0; ++down) {
            const Cell below = cellAt(world, feet.offset(0, -1, 0));
            if (below == Cell::Unloaded || below == Cell::Floor || below == Cell::Blocked) {
                break;
            }
            feet = feet.offset(0, -1, 0);
        }
        if (canStand(world, feet, search.cells)) {
            return middleOf(feet);
        }
    }
    return std::nullopt;
}

std::optional<QcVec3> nearest(const QcBlockAccess &world, const QcVec3 &point, const QcVec3 &along, int cells)
{
    const std::optional<QcBlockPos> entry = openCellBefore(world, point, along);
    if (!entry) {
        return std::nullopt;
    }

    // The body holds the open cell: feet in it, else lower (under a roof); then the cells beside it.
    static constexpr std::array<std::array<int, 2>, 5> sides{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    for (const auto &[dx, dz] : sides) {
        for (int down = 0; down < cells; ++down) {
            const QcBlockPos feet = entry->offset(dx, -down, dz);
            if (fits(world, feet, cells)) {
                return middleOf(feet);
            }
        }
    }
    return std::nullopt;
}

} // namespace QcSafeSpot
