#include "worldgen/villages/QcVillageForecourt.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <functional>
#include <optional>

namespace {

using Rect = QcVillageDraft::Rect;
constexpr int roadHalfWidth = QcVillageDraft::roadHalfWidth;
// A forecourt reaches at most this far.
constexpr int maxForecourt = 12;

// The columns (x when the front row runs along x, else z) of the doorsteps in the row `row` in
// front of a piece; `middle` when none lies there.
QList<int> doorColumns(const QList<QcBlockPos> &doorsteps, bool alongX, int row, int middle)
{
    QList<int> doors;
    for (const QcBlockPos &at : doorsteps) {
        if ((alongX ? at.z : at.x) == row) {
            doors.append(alongX ? at.x : at.z);
        }
    }
    if (doors.isEmpty()) {
        doors.append(middle);
    }
    return doors;
}

// One column of a forecourt, cellAt(1), cellAt(2), ... up to the first road cell; empty when it
// does not reach a road within the forecourt's reach, std::nullopt when it crosses a piece or water.
std::optional<QList<std::pair<int, int>>> forecourtColumn(const QcTerrainGenerator &terrain, const QcVillageDraft &draft,
                                                          const std::function<std::pair<int, int>(int)> &cellAt)
{
    QList<std::pair<int, int>> column;
    for (int j = 1; j <= maxForecourt; ++j) {
        const auto [x, z] = cellAt(j);
        if (draft.roadCells.contains(QcVillageDraft::cellKey(x, z))) {
            return column;
        }
        const bool onPiece = std::ranges::any_of(draft.pieces, [&](Rect piece) { return piece.intersects({x, z, x, z}, 0); });
        if (onPiece || terrain.column(x, z).height - 1 < terrain.seaLevel()) {
            return std::nullopt;
        }
        column.append({x, z});
    }
    return QList<std::pair<int, int>>{};
}

} // namespace

namespace QcVillageForecourt {

Rect behindFront(std::pair<int, int> middle, std::pair<int, int> facing, std::pair<int, int> size)
{
    const auto [mx, mz] = middle;
    const auto [sizeX, sizeZ] = size;
    const int x0 = facing.first == 0 ? mx - sizeX / 2 : (facing.first < 0 ? mx : mx - sizeX + 1);
    const int z0 = facing.second == 0 ? mz - sizeZ / 2 : (facing.second < 0 ? mz : mz - sizeZ + 1);
    return {x0, z0, x0 + sizeX - 1, z0 + sizeZ - 1};
}

QList<std::pair<int, int>> forecourt(const QcTerrainGenerator &terrain, const QcVillageDraft &draft, Rect rect, std::pair<int, int> facing,
                                     const QList<QcBlockPos> &doorsteps)
{
    // Straight out to the road from the columns of the front row next to a door: those beside it
    // that do not reach the road within the forecourt's reach stay grass, the door's own must.
    const bool alongX = facing.first == 0;
    const int low = alongX ? rect.x0 : rect.z0;
    const int high = alongX ? rect.x1 : rect.z1;
    const int front = alongX ? (facing.second < 0 ? rect.z0 : rect.z1) : (facing.first < 0 ? rect.x0 : rect.x1);
    const int step = alongX ? facing.second : facing.first;
    const QList<int> doors = doorColumns(doorsteps, alongX, front + step, (low + high) / 2);

    QList<std::pair<int, int>> cells;
    for (int u = low; u <= high; ++u) {
        if (std::ranges::none_of(doors, [u](int door) { return std::abs(u - door) <= roadHalfWidth; })) {
            continue;
        }
        const auto cellAt = [&](int j) { return alongX ? std::pair{u, front + step * j} : std::pair{front + step * j, u}; };
        const std::optional<QList<std::pair<int, int>>> column = forecourtColumn(terrain, draft, cellAt);
        if (!column || (column->isEmpty() && doors.contains(u))) {
            return {};
        }
        cells.append(*column);
    }
    return cells;
}

} // namespace QcVillageForecourt
