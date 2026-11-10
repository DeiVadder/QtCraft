// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/strongholds/QcStrongholdBlocks.h"

#include "worldgen/generators/QcStateResolver.h"

using namespace Qt::StringLiterals;

namespace {

// The names of the sides, north, east, south and west.
const std::array<QString, 4> &sideNames()
{
    static const std::array<QString, 4> names{u"north"_s, u"east"_s, u"south"_s, u"west"_s};
    return names;
}

} // namespace

QcStrongholdBlocks::Resolved QcStrongholdBlocks::resolve(const QcStrongholdSettings &settings, const QcContentRegistry &content)
{
    QcStateResolver resolver(content, u"stronghold"_s, u"no strongholds"_s);
    const auto state = [&resolver](const QString &name) { return resolver.state(name); };
    // The states of `name` with `property` set to each side.
    const auto bySide = [&resolver](const QString &name, const QString &property) {
        BySide states{};
        for (std::size_t side = 0; side < sideNames().size(); ++side) {
            states[side] = resolver.state(u"%1[%2=%3]"_s.arg(name, property, sideNames()[side]));
        }
        return states;
    };

    QcStrongholdBlocks blocks;
    blocks.bricks = state(settings.bricks);
    blocks.cracked = state(settings.cracked);
    blocks.mossy = state(settings.mossy);
    blocks.chiseled = state(settings.chiseled);
    blocks.infested = state(settings.infested);
    blocks.slab = state(settings.slab + u"[type=bottom]"_s);
    blocks.barsAlongX = state(settings.bars + u"[east=true,west=true]"_s);
    blocks.barsAlongZ = state(settings.bars + u"[north=true,south=true]"_s);
    blocks.bookshelf = state(settings.bookshelf);
    blocks.planks = state(settings.planks);
    blocks.fenceAlongX = state(settings.fence + u"[east=true,west=true]"_s);
    blocks.fenceAlongZ = state(settings.fence + u"[north=true,south=true]"_s);
    blocks.cobweb = state(settings.cobweb);
    blocks.water = state(settings.water);
    blocks.lava = state(settings.lava);
    blocks.spawner = state(settings.spawner);
    blocks.buttons = bySide(settings.button, u"attach"_s);
    blocks.ladders = bySide(settings.ladder, u"attach"_s);
    blocks.torches = bySide(settings.torch, u"attach"_s);
    blocks.stairs = resolver.facing(settings.stairs, u",half=bottom,shape=straight"_s);
    blocks.woodDoorLower = resolver.facing(settings.woodDoor, u",half=bottom"_s);
    blocks.woodDoorUpper = resolver.facing(settings.woodDoor, u",half=top"_s);
    blocks.ironDoorLower = resolver.facing(settings.ironDoor, u",half=bottom"_s);
    blocks.ironDoorUpper = resolver.facing(settings.ironDoor, u",half=top"_s);
    blocks.chests = resolver.facing(settings.chest);
    blocks.frames = resolver.facing(settings.portalFrame, u",eye=false"_s);
    blocks.framesWithEye = resolver.facing(settings.portalFrame, u",eye=true"_s);
    if (!resolver.resolved()) {
        return {std::nullopt, resolver.warnings()};
    }
    return {blocks, resolver.warnings()};
}
