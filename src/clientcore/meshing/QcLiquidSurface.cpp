#include "clientcore/meshing/QcLiquidSurface.h"

#include <cstddef>

namespace QcLiquidSurface {

namespace {

// The weighted sum of the heights of a corner and their weights.
struct Average
{
    float sum = 0.0f;
    float weight = 0.0f;

    void add(float height)
    {
        if (height >= heavyFrom) {
            sum += height * heavyWeight;
            weight += heavyWeight;
        } else if (height >= 0.0f) {
            sum += height;
            weight += 1.0f;
        }
    }
};

float cellAt(const std::array<float, 9> &cells, int dx, int dz)
{
    return cells[static_cast<std::size_t>(dz + 1) * 3 + static_cast<std::size_t>(dx + 1)];
}

float cornerHeight(const std::array<float, 9> &cells, int dx, int dz)
{
    const float own = cellAt(cells, 0, 0);
    const float alongX = cellAt(cells, dx, 0);
    const float alongZ = cellAt(cells, 0, dz);
    if (own >= 1.0f || alongX >= 1.0f || alongZ >= 1.0f) {
        return 1.0f;
    }

    Average average;
    // The diagonal cell counts only where the liquid reaches it past one of the side cells.
    if (alongX > 0.0f || alongZ > 0.0f) {
        const float diagonal = cellAt(cells, dx, dz);
        if (diagonal >= 1.0f) {
            return 1.0f;
        }
        average.add(diagonal);
    }
    average.add(own);
    average.add(alongX);
    average.add(alongZ);
    return average.weight > 0.0f ? average.sum / average.weight : own;
}

} // namespace

std::array<float, 4> cornerHeights(const std::array<float, 9> &cells)
{
    std::array<float, 4> corners{};
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 2; ++x) {
            corners[static_cast<std::size_t>(x) + 2 * static_cast<std::size_t>(z)] = cornerHeight(cells, x * 2 - 1, z * 2 - 1);
        }
    }
    return corners;
}

} // namespace QcLiquidSurface
