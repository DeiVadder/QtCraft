#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

#include <optional>

class QcContentRegistry;

// Maps in the crafting grid (decision E-804), besides their recipes: a filled map with empty maps
// of its kind and nothing else copies it (one copy per empty map, all with its map id); a filled
// map in the middle of the 3 x 3 grid with its `zoomsWith` item in the eight cells around it zooms
// it out (a new map one scale larger, which the server makes when the result is taken).
namespace QcMapCrafting {

struct Result
{
    enum class Kind : quint8 { Copy, Zoom };
    Kind kind = Kind::Copy;
    // Copy: the copies with the original (count) and the map id. Zoom: one filled map with the id
    // of the map to zoom out.
    QcItemStack stack;
};

// What the grid cells (row by row, `gridSize` wide) make, or nothing when they are no map crafting.
[[nodiscard]] std::optional<Result> result(const QcContentRegistry &content, const QList<QcItemStack> &grid, int gridSize);

} // namespace QcMapCrafting
