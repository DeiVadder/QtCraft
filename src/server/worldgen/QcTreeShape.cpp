#include "QcTreeShape.h"

#include "QcNoise.h"

#include <algorithm>
#include <cmath>

namespace QcTreeShape {

int height(const Params &params, double random)
{
    return params.minHeight + int(random * (params.maxHeight - params.minHeight + 1));
}

void build(const Params &params, QcBlockPos base, double random, quint64 seed, const Put &put)
{
    if (params.type == QcFeatureDefinition::Type::Plant) {
        put(base, params.trunk, true);
        return;
    }
    const int trunkHeight = height(params, random);
    for (int dy = 0; dy < trunkHeight; ++dy) {
        put(base.offset(0, dy, 0), params.trunk, false);
    }
    if (params.type != QcFeatureDefinition::Type::Tree) {
        return;
    }
    if (params.cone) {
        // Spruce-like: rings shrinking towards the top, starting two blocks above the ground.
        const int crownBottom = base.y + 2;
        const int crownTop = base.y + trunkHeight;
        for (int y = crownBottom; y <= crownTop; ++y) {
            const int radius = std::max(0, std::min(3, (crownTop - y + 2) / 2));
            for (int dx = -radius; dx <= radius; ++dx) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    if (std::abs(dx) + std::abs(dz) <= radius + 1) {
                        put({base.x + dx, y, base.z + dz}, params.leaves, true);
                    }
                }
            }
        }
        return;
    }
    // Round crown: two wide layers below the top, two narrow layers at the top.
    const int top = base.y + trunkHeight;
    for (int y = top - 3; y <= top; ++y) {
        const int radius = y >= top - 1 ? 1 : 2;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
                if (corner && (y == top || qcPositionRandom(seed, base.x + dx, y, base.z + dz) < 0.5)) {
                    continue;
                }
                put({base.x + dx, y, base.z + dz}, params.leaves, true);
            }
        }
    }
}

} // namespace QcTreeShape
