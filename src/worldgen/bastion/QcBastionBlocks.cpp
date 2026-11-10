// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/bastion/QcBastionBlocks.h"

#include "worldgen/generators/QcStateResolver.h"

using namespace Qt::StringLiterals;

QcBastionBlocks::Resolved QcBastionBlocks::resolve(const QcNetherSettings::Bastion &settings, const QcContentRegistry &content)
{
    QcStateResolver resolver(content, u"bastion"_s, u"no bastions"_s);
    const auto state = [&resolver](const QString &name) { return resolver.state(name); };

    QcBastionBlocks blocks;
    blocks.rough = state(settings.rough);
    blocks.brick = state(settings.brick);
    blocks.cracked = state(settings.cracked);
    blocks.polished = state(settings.polished);
    blocks.chiseled = state(settings.chiseled);
    blocks.gilded = state(settings.gilded);
    blocks.gold = state(settings.gold);
    blocks.soil = state(settings.soil);
    blocks.lava = state(settings.lava);
    blocks.lantern = state(settings.lantern + u"[attach=floor]"_s);
    blocks.spawner = state(settings.spawner);
    blocks.wallAlongX = state(settings.wall + u"[east=true,west=true,up=false]"_s);
    blocks.wallAlongZ = state(settings.wall + u"[north=true,south=true,up=false]"_s);
    blocks.chests = resolver.facing(settings.chest);
    blocks.stairs = resolver.facing(settings.stairs, u",half=bottom,shape=straight"_s);
    if (!resolver.resolved()) {
        return {std::nullopt, resolver.warnings()};
    }

    blocks.open = resolver.openStates();
    return {std::move(blocks), resolver.warnings()};
}
