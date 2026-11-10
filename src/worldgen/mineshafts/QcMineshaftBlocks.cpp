// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/mineshafts/QcMineshaftBlocks.h"

#include "worldgen/generators/QcStateResolver.h"

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<const char *, 4> sideNames{"north", "east", "south", "west"};

} // namespace

QcMineshaftBlocks::Side QcMineshaftBlocks::sideOf(int dx, int dz)
{
    if (dz < 0) {
        return Side::North;
    }
    if (dx > 0) {
        return Side::East;
    }
    return dz > 0 ? Side::South : Side::West;
}

QcMineshaftBlocks::Resolved QcMineshaftBlocks::resolve(const QcMineshaftSettings &settings, const QcContentRegistry &content)
{
    QcStateResolver resolver(content, u"mineshafts"_s, u"no mine shafts"_s);
    const auto state = [&resolver](const QString &name) { return resolver.state(name); };

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
        blocks.railsUp[side] = state(settings.rail + u"[facing="_s + name + u",shape=ascending]"_s);
    }
    if (!resolver.resolved()) {
        return {std::nullopt, resolver.warnings()};
    }

    blocks.open = resolver.openStates();
    blocks.liquid = resolver.everyState([](const QcBlockDefinition &block) { return block.liquid; });
    blocks.falling = resolver.everyState([](const QcBlockDefinition &block) { return block.falls; });
    blocks.foliage = resolver.everyState([](const QcBlockDefinition &block) { return block.solid && !block.opaque && !block.liquid; });
    const QcSpawnerDefinition &spawner = content.block(blocks.spawner).spawner;
    blocks.areaX = spawner.areaX / 2;
    blocks.areaY = spawner.areaY / 2;
    blocks.areaZ = spawner.areaZ / 2;
    return {std::move(blocks), resolver.warnings()};
}
