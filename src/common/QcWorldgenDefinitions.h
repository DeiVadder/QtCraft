#pragma once

// World-generation content: biomes, terrain features, ore veins and global terrain settings.
// Plain data structs grouped in one header because they are only meaningful together; block and
// feature references are content names resolved by the generator.

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
    // Climate point in [0, 1]; each column takes the biome closest to its local climate.
    double temperature = 0.5;
    double humidity = 0.5;
    // Surface height in blocks for land in this biome.
    double baseHeight = 68.0;
    double heightVariation = 6.0;
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
    QList<QcFeaturePlacement> features;
};

struct QcFeatureDefinition
{
    enum class Type { Tree, Column };

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
};
