#include "worldgen/mineshafts/QcMineshaftBlocks.h"

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<const char *, 4> sideNames{"north", "east", "south", "west"};

} // namespace

int QcMineshaftBlocks::sideOf(int dx, int dz)
{
    if (dz < 0) {
        return 0;
    }
    if (dx > 0) {
        return 1;
    }
    return dz > 0 ? 2 : 3;
}

QcMineshaftBlocks::Resolved QcMineshaftBlocks::resolve(const QcMineshaftSettings &settings, const QcContentRegistry &content)
{
    QStringList warnings;
    bool resolved = true;
    const auto state = [&](const QString &name) {
        const std::optional<QcBlockId> id = content.stateId(name);
        if (!id) {
            warnings.append(u"mineshafts: unknown block '%1', no mine shafts"_s.arg(name));
        }
        resolved = resolved && id.has_value();
        return id.value_or(qcAirBlockId);
    };

    QcMineshaftBlocks blocks;
    blocks.planks = state(settings.planks);
    blocks.post = state(settings.post);
    blocks.railNorthSouth = state(settings.rail + u"[facing=north,shape=straight]"_s);
    blocks.railEastWest = state(settings.rail + u"[facing=east,shape=straight]"_s);
    blocks.cobweb = state(settings.cobweb);
    blocks.seal = state(settings.seal);
    blocks.spawner = state(settings.spawner);
    for (std::size_t side = 0; side < sideNames.size(); ++side) {
        const QString name = QString::fromLatin1(sideNames[side]);
        blocks.lights[side] = state(settings.light + u"[attach="_s + name + u']');
        blocks.ladders[side] = state(settings.ladder + u"[attach="_s + name + u']');
    }
    if (!resolved) {
        return {std::nullopt, warnings};
    }

    blocks.open.resize(static_cast<std::size_t>(content.stateCount()));
    blocks.liquid.resize(blocks.open.size());
    blocks.falling.resize(blocks.open.size());
    blocks.foliage.resize(blocks.open.size());
    for (std::size_t id = 0; id < blocks.open.size(); ++id) {
        const QcBlockDefinition &block = content.block(static_cast<QcBlockId>(id));
        blocks.open[id] = !block.solid || block.liquid;
        blocks.liquid[id] = block.liquid;
        blocks.falling[id] = block.falls;
        blocks.foliage[id] = block.solid && !block.opaque && !block.liquid;
    }
    const QcSpawnerDefinition &spawner = content.block(blocks.spawner).spawner;
    blocks.areaX = spawner.areaX / 2;
    blocks.areaY = spawner.areaY / 2;
    blocks.areaZ = spawner.areaZ / 2;
    return {std::move(blocks), warnings};
}
