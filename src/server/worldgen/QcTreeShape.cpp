#include "QcTreeShape.h"

#include "QcNoise.h"

#include <algorithm>
#include <cmath>

namespace QcTreeShape {

int height(const Params &params, double random)
{
    return params.minHeight + int(random * (params.maxHeight - params.minHeight + 1));
}

namespace {

// One ring of a cone crown around `centre`.
void buildConeLayer(QcBlockId leaves, QcBlockPos centre, int radius, const Put &put)
{
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            if (std::abs(dx) + std::abs(dz) <= radius + 1) {
                put({centre.x + dx, centre.y, centre.z + dz}, leaves, true);
            }
        }
    }
}

// Spruce-like: rings shrinking towards the top, starting two blocks above the ground.
void buildConeCrown(const Params &params, QcBlockPos base, int trunkHeight, const Put &put)
{
    const int crownBottom = base.y + 2;
    const int crownTop = base.y + trunkHeight;
    for (int y = crownBottom; y <= crownTop; ++y) {
        const int radius = std::max(0, std::min(3, (crownTop - y + 2) / 2));
        buildConeLayer(params.leaves, {base.x, y, base.z}, radius, put);
    }
}

// One layer of a round crown, `depth` layers below its top: narrow at the top two, ragged corners
// below the top.
void buildRoundLayer(QcBlockId leaves, QcBlockPos centre, int depth, quint64 seed, const Put &put)
{
    const int radius = depth <= 1 ? 1 : 2;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
            if (corner && (depth == 0 || qcPositionRandom(seed, centre.x + dx, centre.y, centre.z + dz) < 0.5)) {
                continue;
            }

            put({centre.x + dx, centre.y, centre.z + dz}, leaves, true);
        }
    }
}

// Round crown: two wide layers below the top, two narrow layers at the top.
void buildRoundCrown(const Params &params, QcBlockPos base, int trunkHeight, quint64 seed, const Put &put)
{
    const int top = base.y + trunkHeight;
    for (int y = top - 3; y <= top; ++y) {
        buildRoundLayer(params.leaves, {base.x, y, base.z}, top - y, seed, put);
    }
}

} // namespace

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
        buildConeCrown(params, base, trunkHeight, put);
    } else {
        buildRoundCrown(params, base, trunkHeight, seed, put);
    }
}

} // namespace QcTreeShape
