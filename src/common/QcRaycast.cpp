#include "QcRaycast.h"

#include "QcContentRegistry.h"

#include <cmath>
#include <limits>

namespace QcRaycast {

std::optional<QcRaycastHit> cast(const QcBlockAccess &world, const QcVec3 &origin, const QcVec3 &direction, double maxDistance)
{
    const QcVec3 dir = direction.normalized();
    if (dir.length() == 0.0) {
        return std::nullopt;
    }
    QcBlockPos cell{int(std::floor(origin.x)), int(std::floor(origin.y)), int(std::floor(origin.z))};
    const int stepX = dir.x > 0 ? 1 : (dir.x < 0 ? -1 : 0);
    const int stepY = dir.y > 0 ? 1 : (dir.y < 0 ? -1 : 0);
    const int stepZ = dir.z > 0 ? 1 : (dir.z < 0 ? -1 : 0);
    constexpr double infinity = std::numeric_limits<double>::infinity();
    // Distance along the ray between two grid planes per axis, and to the first plane crossing.
    const double deltaX = stepX != 0 ? std::abs(1.0 / dir.x) : infinity;
    const double deltaY = stepY != 0 ? std::abs(1.0 / dir.y) : infinity;
    const double deltaZ = stepZ != 0 ? std::abs(1.0 / dir.z) : infinity;
    double nextX = stepX > 0 ? (cell.x + 1 - origin.x) * deltaX : (stepX < 0 ? (origin.x - cell.x) * deltaX : infinity);
    double nextY = stepY > 0 ? (cell.y + 1 - origin.y) * deltaY : (stepY < 0 ? (origin.y - cell.y) * deltaY : infinity);
    double nextZ = stepZ > 0 ? (cell.z + 1 - origin.z) * deltaZ : (stepZ < 0 ? (origin.z - cell.z) * deltaZ : infinity);

    const QcContentRegistry &content = world.content();
    double travelled = 0.0;
    QcBlockFace face = QcBlockFace::Up;
    while (travelled <= maxDistance) {
        if (cell.y >= 0 && cell.y < QcWorld::worldHeight) {
            const std::optional<QcBlockId> block = world.blockAt(cell);
            if (!block) {
                return std::nullopt;
            }
            if (content.isValidBlock(*block)) {
                const QcBlockDefinition &definition = content.block(*block);
                if (*block != qcAirBlockId && !definition.liquid && definition.renderLayer != QcRenderLayer::Invisible) {
                    return QcRaycastHit{cell, face, travelled};
                }
            }
        }
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
    return std::nullopt;
}

} // namespace QcRaycast
