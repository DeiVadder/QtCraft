// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressBlocks.h"

#include "worldgen/generators/QcStateResolver.h"

#include <array>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<const char *, 4> sideNames{"north", "east", "south", "west"};

} // namespace

QcFortressBlocks::Resolved QcFortressBlocks::resolve(const QcNetherSettings::Fortress &settings, const QcContentRegistry &content)
{
    QcStateResolver resolver(content, u"fortress"_s, u"no fortresses"_s);
    const auto state = [&resolver](const QString &name) { return resolver.state(name); };

    QcFortressBlocks blocks;
    blocks.brick = state(settings.brick);
    blocks.cracked = state(settings.cracked);
    blocks.chiseled = state(settings.chiseled);
    blocks.lane = state(settings.lane);
    blocks.light = state(settings.light);
    blocks.soil = state(settings.soil);
    blocks.spawner = state(settings.spawner);
    blocks.fenceAlongX = state(settings.fence + u"[east=true,west=true]"_s);
    blocks.fenceAlongZ = state(settings.fence + u"[north=true,south=true]"_s);
    blocks.fencePost = state(settings.fence);
    for (std::size_t i = 0; i < sideNames.size(); ++i) {
        const QString side = QString::fromLatin1(sideNames[i]);
        blocks.crops[i] = state(settings.crop + u"[age=%1]"_s.arg(i));
        blocks.chests[i] = state(settings.chest + u"[facing="_s + side + u']');
        blocks.stairs[i] = state(settings.stairs + u"[facing="_s + side + u",half=bottom,shape=straight]"_s);
    }
    if (!resolver.resolved()) {
        return {std::nullopt, resolver.warnings()};
    }

    blocks.open = resolver.openStates();
    return {std::move(blocks), resolver.warnings()};
}
