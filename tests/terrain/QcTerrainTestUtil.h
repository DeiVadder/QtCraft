// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "fixtures/QcTestUtil.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <memory>

// Helpers of the terrain generation tests (test_terrain_generator, test_terrain_liquids): a block of
// the test content and minimal content with one flat biome.
namespace QcTerrainTestUtil {

inline QcBlockDefinition simpleBlock(const QString &name)
{
    QcBlockDefinition block;
    block.name = name;
    block.faceTextures.fill(name);
    return block;
}

// Minimal content: stone, water, a red surface block and one flat biome; caves optional.
inline std::shared_ptr<QcContentRegistry> flatContent(bool caves)
{
    using namespace Qt::StringLiterals;
    auto content = std::make_shared<QcContentRegistry>();
    for (const QString &name : {u"t:stone"_s, u"t:red"_s, u"t:ore"_s, u"t:bedrock"_s}) {
        if (!content->registerBlock(simpleBlock(name))) {
            QcTestUtil::failCase(QStringLiteral("cannot register block"));
        }
    }

    QcBlockDefinition water = simpleBlock(u"t:water"_s);
    water.solid = false;
    water.liquid = true;
    if (!content->registerBlock(water)) {
        QcTestUtil::failCase(QStringLiteral("cannot register water"));
    }

    QcTerrainSettings terrain;
    terrain.stoneBlock = u"t:stone"_s;
    terrain.waterBlock = u"t:water"_s;
    terrain.bedrockBlock = u"t:bedrock"_s;
    terrain.caves = caves;
    content->worldgen().setTerrainSettings(terrain);
    QcBiomeDefinition biome;
    biome.name = u"t:flat"_s;
    biome.baseHeight = 80.0;
    biome.heightVariation = 0.0;
    biome.surfaceBlock = u"t:red"_s;
    biome.subsurfaceBlock = u"t:red"_s;
    biome.subsurfaceDepth = 2;
    content->worldgen().registerBiome(biome);
    return content;
}

} // namespace QcTerrainTestUtil
