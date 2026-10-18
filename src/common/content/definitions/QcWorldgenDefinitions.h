#pragma once

// World-generation content: biomes, terrain features, ore veins and global terrain settings.
// Plain data structs grouped in one header because they are only meaningful together; block and
// feature references are content names resolved by the generator.

#include "common/content/definitions/QcDungeonDefinition.h"
#include "common/content/definitions/QcMineshaftDefinition.h"

#include <QList>
#include <QString>

struct QcFeaturePlacement
{
    QString feature;
    // Probability per surface column.
    double density = 0.0;
};

struct QcBiomeDefinition
{
    QString name;
    QString translationKey;
    // Translation key of the handbook's text about it ("<translation>.description" unless set); a
    // missing text leaves only the facts. Server side only.
    QString descriptionKey;
    // Climate point in [0, 1]; each column takes the biome closest to its local climate.
    double temperature = 0.5;
    double humidity = 0.5;
    // Third climate coordinate in [0, 1]: how rugged the land is. The world's ruggedness is low
    // almost everywhere and high along chains, so a biome with a high value forms mountain ranges
    // across all climates. Counts twice as much as temperature and humidity.
    double ruggedness = 0.2;
    // Surface height in blocks for land in this biome.
    double baseHeight = 68.0;
    double heightVariation = 6.0;
    // Share of the variation in [0, 1] that comes from sharp ridges instead of rolling hills.
    double ridges = 0.0;
    // Overhangs: a 3D noise of this many blocks reshapes the ground around the surface (ledges,
    // arches, overhanging cliffs); 0 keeps plain columns.
    int overhang = 0;
    // Height of terrace steps in blocks (0: none). Where a slowly changing mask allows it, the land
    // rises in flat steps with cliffs between them.
    int terraceHeight = 0;
    // Surface block where the ground drops at least `steepSlope` blocks to a neighbour (rock on
    // cliffs); empty keeps the normal surface.
    QString steepBlock;
    int steepSlope = 3;
    QString surfaceBlock;
    QString subsurfaceBlock;
    int subsurfaceDepth = 3;
    QString underwaterBlock;
    QString beachBlock;
    // Water surface block in cold biomes (e.g. ice); empty keeps water.
    QString frozenWaterBlock;
    // Surface above this height uses `snowBlock` (0 disables).
    int snowLine = 0;
    QString snowBlock;
    // Villages may be built here (decision E-403), in the style that names the biome or else the
    // fallback style. Server side only.
    bool villages = false;
    QList<QcFeaturePlacement> features;
};

struct QcFeatureDefinition
{
    // Plant: a single block (`trunkBlock`, given as "block") on the ground. Pool: a liquid
    // (`trunkBlock`, given as "block") filling the bottom of a cave; pools are tried in every chunk
    // and are not listed by biomes.
    enum class Type { Tree, Column, Plant, Pool };

    QString name;
    Type type = Type::Tree;
    QString trunkBlock;
    QString leavesBlock;
    int minHeight = 4;
    int maxHeight = 6;
    // "round" or "cone" crowns for trees.
    QString shape = QStringLiteral("round");
    // Blocks the feature may stand on; empty allows any solid block.
    QList<QString> groundBlocks;
    // Only on ground at sea level with water right next to it (sugar cane on a shore).
    bool nearWater = false;
    // Size in blocks of the patches the feature grows in (flower meadows, groves); 0 scatters it
    // evenly. The average density stays the same.
    int patchSize = 0;
    // Pools: `attempts` tries per chunk, each taken with `chance`, at a cave floor between minY and
    // maxY; a basin of up to `radius` blocks is sunk `depth` blocks into the floor.
    int minY = 8;
    int maxY = 40;
    int attempts = 1;
    double chance = 0.0;
    int radius = 3;
    int depth = 2;
    // Pools on the surface of dry land instead of cave floors (lava lakes); minY/maxY do not apply.
    bool surface = false;
    // Biomes a pool may lie in; empty for all.
    QList<QString> biomes;
};

struct QcOreDefinition
{
    QString name;
    QString block;
    QString replaces;
    int minY = 1;
    int maxY = 64;
    int veinsPerChunk = 8;
    int veinSize = 6;
    // Biomes a vein may lie in (the biome of the column where it starts); empty for all (decision
    // E-453: infested stone only in the mountains).
    QList<QString> biomes;
};

struct QcTerrainSettings
{
    int seaLevel = 62;
    QString stoneBlock;
    QString waterBlock;
    QString bedrockBlock;
    bool caves = true;
    // Larger values make the spaghetti tunnels wider (threshold on two noise fields).
    double caveTunnelWidth = 0.075;
    // Lower values make the open caverns larger (threshold on one noise field).
    double caveCavernThreshold = 0.58;
    // Cave air at or below this height is filled with `caveLiquid` (lava lakes deep down); an empty
    // block name keeps the caves dry.
    QString caveLiquidBlock;
    int caveLiquidLevel = 10;
    // Caves break through the ground only where an entrance noise allows it; elsewhere this many
    // blocks of rock stay above them.
    int caveRoof = 4;
    // Share of the land, about, where caves may open to the surface.
    double caveEntrances = 0.04;
    // Bedrock: the bottom layer is solid, the layers above it get thinner up to this many layers.
    int bedrockLayers = 3;
    // Dungeons with spawners and corridors to caves (decision E-522).
    QcDungeonSettings dungeons;
    // Mine shafts with levels, staircases, bridges and surface entrances (decision E-570).
    QcMineshaftSettings mineshafts;
};
