#include "worldgen/terrain/QcTreeShape.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTreeLayout.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcTreeShape {

std::optional<Crown> crownOf(const QString &shape)
{
    static constexpr std::array<std::pair<std::u16string_view, Crown>, 5> names{{{u"round", Crown::Round},
                                                                                 {u"cone", Crown::Cone},
                                                                                 {u"bush", Crown::Bush},
                                                                                 {u"giant_cone", Crown::GiantCone},
                                                                                 {u"giant_round", Crown::GiantRound}}};
    for (const auto &[name, crown] : names) {
        if (shape == name) {
            return crown;
        }
    }
    return std::nullopt;
}

std::array<QcBlockId, 4> vinesOf(const QcContentRegistry &content, QcBlockId vine)
{
    if (vine == qcAirBlockId) {
        return {};
    }
    return {content.withProperty(vine, u"attach"_s, u"north"_s), content.withProperty(vine, u"attach"_s, u"east"_s),
            content.withProperty(vine, u"attach"_s, u"south"_s), content.withProperty(vine, u"attach"_s, u"west"_s)};
}

std::pair<QcBlockId, QcBlockId> lyingTrunks(const QcContentRegistry &content, QcBlockId trunk)
{
    return {content.withProperty(trunk, u"axis"_s, u"x"_s), content.withProperty(trunk, u"axis"_s, u"z"_s)};
}

Params paramsOf(const QcContentRegistry &content, const QcFeatureDefinition &feature)
{
    Params params;
    params.type = feature.type;
    params.trunk = content.blockId(feature.trunkBlock).value_or(qcAirBlockId);
    params.leaves = content.blockId(feature.leavesBlock).value_or(qcAirBlockId);
    params.minHeight = feature.minHeight;
    params.maxHeight = feature.maxHeight;
    params.crown = crownOf(feature.shape).value_or(Crown::Round);
    std::tie(params.trunkX, params.trunkZ) = lyingTrunks(content, params.trunk);
    params.vines = vinesOf(content, content.blockId(feature.vinesBlock).value_or(qcAirBlockId));
    return params;
}

int reach(const Params &params)
{
    if (params.type != QcFeatureDefinition::Type::Tree) {
        return 0;
    }

    // Vines hang one block beside the crown; giant crowns leave room for them.
    const int vines = params.vines[0] != qcAirBlockId ? 1 : 0;
    switch (params.crown) {
    case Crown::Round:
    case Crown::Bush:
        return 2 + vines;
    case Crown::Cone:
        return 3 + vines;
    case Crown::GiantCone:
    case Crown::GiantRound:
        return 1 + QcTreeLayout::giantCrownRadius;
    }
    return 0;
}

int height(const Params &params, double random)
{
    return params.minHeight + int(random * (params.maxHeight - params.minHeight + 1));
}

namespace {

// One ring of a cone crown around `centre`.
void buildConeLayer(QcBlockId leaves, QcBlockPos centre, int radius, QcTreeLayout &layout)
{
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            if (std::abs(dx) + std::abs(dz) <= radius + 1) {
                layout.leaf({centre.x + dx, centre.y, centre.z + dz}, leaves, false);
            }
        }
    }
}

// Spruce-like: rings shrinking towards the top, starting two blocks above the ground.
void buildConeCrown(const Params &params, QcBlockPos base, int trunkHeight, QcTreeLayout &layout)
{
    const int crownBottom = base.y + 2;
    const int crownTop = base.y + trunkHeight;
    for (int y = crownBottom; y <= crownTop; ++y) {
        const int radius = std::max(0, std::min(3, (crownTop - y + 2) / 2));
        buildConeLayer(params.leaves, {base.x, y, base.z}, radius, layout);
    }
}

// Whether (dx, dz) belongs to the layer of a round crown `depth` layers below its top: narrow at
// the top two, ragged corners below the top.
bool inRoundLayer(QcBlockPos centre, int depth, quint64 seed, int dx, int dz)
{
    const int radius = depth <= 1 ? 1 : 2;
    if (std::abs(dx) > radius || std::abs(dz) > radius) {
        return false;
    }

    const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
    return !corner || (depth != 0 && qcPositionRandom(seed, centre.x + dx, centre.y, centre.z + dz) >= 0.5);
}

// Round crown: two wide layers below the top, two narrow layers at the top. Vines hang from the
// lowest layer.
void buildRoundCrown(const Params &params, QcBlockPos base, int trunkHeight, quint64 seed, QcTreeLayout &layout)
{
    const int top = base.y + trunkHeight;
    for (int y = top - 3; y <= top; ++y) {
        const QcBlockPos centre{base.x, y, base.z};
        for (int cell = 0; cell < 25; ++cell) {
            // The 5 × 5 cells around the centre as one index.
            const int dx = cell % 5 - 2;
            const int dz = cell / 5 - 2;
            if (inRoundLayer(centre, top - y, seed, dx, dz)) {
                layout.leaf(centre.offset(dx, 0, dz), params.leaves, y == top - 3);
            }
        }
    }
}

// A bush: leaves around and over its logs, two blocks wide at the bottom, one at the top.
void buildBush(const Params &params, QcBlockPos base, int trunkHeight, quint64 seed, QcTreeLayout &layout)
{
    const int bottom = base.y + trunkHeight - 1;
    for (int dy = 0; dy <= 2; ++dy) {
        layout.squareLayer({base.x, bottom + dy, base.z}, 2 - dy, params.leaves, {seed, 0.6});
    }
}

void buildTrunk(const Params &params, QcBlockPos base, int trunkHeight, QcTreeLayout &layout)
{
    const int width = params.giant() ? 2 : 1;
    for (int dy = 0; dy < trunkHeight; ++dy) {
        for (int ox = 0; ox < width; ++ox) {
            for (int oz = 0; oz < width; ++oz) {
                layout.log(base.offset(ox, dy, oz), params.trunk, true);
            }
        }
    }
}

} // namespace

void build(const Params &params, QcBlockPos base, double random, quint64 seed, const Put &put)
{
    if (params.type == QcFeatureDefinition::Type::Plant) {
        static_cast<void>(put(base, params.trunk, true));
        return;
    }

    QcTreeLayout layout(params, put);
    const int trunkHeight = height(params, random);
    buildTrunk(params, base, trunkHeight, layout);
    if (params.type != QcFeatureDefinition::Type::Tree) {
        return;
    }

    switch (params.crown) {
    case Crown::Round:
        buildRoundCrown(params, base, trunkHeight, seed, layout);
        break;
    case Crown::Cone:
        buildConeCrown(params, base, trunkHeight, layout);
        break;
    case Crown::Bush:
        buildBush(params, base, trunkHeight, seed, layout);
        break;
    case Crown::GiantCone:
        layout.buildGiantCone(params, base, trunkHeight, seed);
        break;
    case Crown::GiantRound:
        layout.buildGiantRound(params, base, trunkHeight, seed);
        break;
    }
    layout.hangVines(params, seed);
}

bool mayReplace(const Params &params, QcBlockId existing, QcBlockId block, bool onlyIntoAir)
{
    if (!onlyIntoAir || existing == qcAirBlockId) {
        return true;
    }
    return std::ranges::contains(params.vines, existing) && !std::ranges::contains(params.vines, block);
}

void buildInChunk(QcChunk &chunk, const Params &params, QcBlockPos base, double random, quint64 seed)
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    build(params, base, random, seed, [&](QcBlockPos pos, QcBlockId block, bool onlyIntoAir) {
        const int lx = pos.x - baseX;
        const int lz = pos.z - baseZ;
        if (pos.y < 0 || pos.y >= QcWorld::worldHeight) {
            return false;
        }
        // Outside the chunk the neighbour decides; vines here count on it as placed.
        if (lx < 0 || lx >= QcWorld::chunkSize || lz < 0 || lz >= QcWorld::chunkSize) {
            return true;
        }
        if (!mayReplace(params, chunk.block(lx, pos.y, lz), block, onlyIntoAir)) {
            return false;
        }
        chunk.setBlock(lx, pos.y, lz, block);
        return true;
    });
}

} // namespace QcTreeShape
