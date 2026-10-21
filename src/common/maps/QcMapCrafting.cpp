#include "common/maps/QcMapCrafting.h"

#include "common/content/registry/QcContentRegistry.h"

namespace {

bool isMap(const QcContentRegistry &content, QcItemStack stack, QcMapItem kind)
{
    return !stack.isEmpty() && content.isValidItem(stack.item) && content.item(stack.item).map == kind;
}

// An empty map that becomes `filled` when used.
bool isEmptyMapOf(const QcContentRegistry &content, QcItemStack stack, QcItemId filled)
{
    return isMap(content, stack, QcMapItem::Empty) && content.itemId(content.item(stack.item).filledMap) == filled;
}

std::optional<QcMapCrafting::Result> copy(const QcContentRegistry &content, const QList<QcItemStack> &grid)
{
    std::optional<QcItemStack> original;
    for (const QcItemStack &cell : grid) {
        if (!isMap(content, cell, QcMapItem::Filled)) {
            continue;
        }
        if (original) {
            return std::nullopt;
        }
        original = cell;
    }
    if (!original || original->damage <= 0) {
        return std::nullopt;
    }

    int copies = 0;
    for (const QcItemStack &cell : grid) {
        if (cell.isEmpty() || isMap(content, cell, QcMapItem::Filled)) {
            continue;
        }
        if (!isEmptyMapOf(content, cell, original->item)) {
            return std::nullopt;
        }
        ++copies;
    }
    if (copies == 0) {
        return std::nullopt;
    }
    return QcMapCrafting::Result{QcMapCrafting::Result::Kind::Copy, {original->item, copies + 1, original->damage}};
}

std::optional<QcMapCrafting::Result> zoom(const QcContentRegistry &content, const QList<QcItemStack> &grid, int gridSize)
{
    constexpr int middle = 4;
    if (gridSize != 3 || grid.size() != 9 || !isMap(content, grid[middle], QcMapItem::Filled) || grid[middle].damage <= 0) {
        return std::nullopt;
    }
    const QcItemDefinition &map = content.item(grid[middle].item);
    const std::optional<QcItemId> material = map.zoomsWith.isEmpty() ? std::nullopt : content.itemId(map.zoomsWith);
    if (!material) {
        return std::nullopt;
    }
    for (int cell = 0; cell < 9; ++cell) {
        if (cell != middle && (grid[cell].isEmpty() || grid[cell].item != *material)) {
            return std::nullopt;
        }
    }
    return QcMapCrafting::Result{QcMapCrafting::Result::Kind::Zoom, {grid[middle].item, 1, grid[middle].damage}};
}

} // namespace

namespace QcMapCrafting {

std::optional<Result> result(const QcContentRegistry &content, const QList<QcItemStack> &grid, int gridSize)
{
    if (std::optional<Result> zoomed = zoom(content, grid, gridSize)) {
        return zoomed;
    }
    return copy(content, grid);
}

} // namespace QcMapCrafting
