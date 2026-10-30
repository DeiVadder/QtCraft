// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// World-generation content: biomes, terrain features, ore veins and global terrain settings.
// Plain data structs grouped in one header because they are only meaningful together; block and
// feature references are content names resolved by the generator.

#include "common/content/worldgen/QcCoastalTowerDefinition.h"
#include "common/content/worldgen/QcDungeonDefinition.h"
#include "common/content/worldgen/QcMineshaftDefinition.h"
#include "common/content/worldgen/QcVegetationDefinition.h"

#include <QList>
#include <QString>

#include <optional>

/*! \brief A feature a biome places (an entry of the biome's \c {"features"}). */
struct QcFeaturePlacement
{
    /*! Qualified name of the feature (\c {"feature"}). */
    QString feature;
    /*! Probability per surface column. */
    double density = 0.0;
};

/*! \brief A block covering a share of some ground (the beds of rivers, decision E-840). */
struct QcBlockShare
{
    /*! Qualified block name (\c {"block"}). */
    QString block;
    /*! Share of the columns in [0, 1], in patches of a few blocks. */
    double share = 0.0;
};

/*! \brief The weight a biome gives a monster kind instead of the kind's own (decision E-838):
    the husk's desert takes most of the zombies' share. */
struct QcSpawnWeight
{
    /*! Qualified name of the monster kind (a key of the biome's \c {"monsters"}). */
    QString mob;
    /*! 0 takes the kind out of the biome's list. */
    double weight = 0.0;
};

/*! \brief A biome of the terrain generator: its climate point, the shape of its land, its
    ground blocks, rivers, deltas, villages, monsters, features and vegetation (a biome file of a
    pack).

    Block and biome references are qualified content names resolved by the generator. */
struct QcBiomeDefinition
{
    /*! Qualified name (\c {"name"}). */
    QString name;
    /*! Translation key of the biome's name (\c {"translation"}, default
        \c {biome.<namespace>.<local name>}). */
    QString translationKey;
    /*! Translation key of the handbook's text about it ("<translation>.description" unless set);
        a missing text leaves only the facts. Server side only. */
    QString descriptionKey;
    /*! The biome's colour on the map preview of a new world (decision E-921), 0xRRGGBB; without
        one the preview spreads hues over the biomes. */
    std::optional<quint32> mapColour;
    /*! Climate point in [0, 1] (\c {"temperature"}, \c {"humidity"}); each column takes the
        biome closest to its local climate. \qcshared */
    double temperature = 0.5;
    double humidity = 0.5;
    /*! Third climate coordinate in [0, 1]: how rugged the land is. The world's ruggedness is low
        almost everywhere and high along chains, so a biome with a high value forms mountain
        ranges across all climates. Counts twice as much as temperature and humidity. */
    double ruggedness = 0.2;
    /*! How rare the biome is, in [0, 1] (decision E-821); 0 for an ordinary biome. The world's
        rarity is 0 almost everywhere and rises to 1 in scattered spots. A biome with a rarity
        takes part in the climate choice only inside those spots (its weight is scaled by the
        world's rarity against its own, so it is exactly 0 elsewhere) and there wins where its
        temperature, humidity and ruggedness fit best (so a temperate rare biome never replaces a
        desert). */
    double rarity = 0.0;
    /*! How far the biome reaches beyond the midpoint to its neighbours in climate space
        (decision E-824): subtracted from its squared climate distance, so its share grows while
        its borders stay straight and it keeps the same neighbours; 0 for an ordinary biome. */
    double spread = 0.0;
    /*! Surface height in blocks for land in this biome (\c {"baseHeight"}). */
    double baseHeight = 68.0;
    /*! Height variation in blocks around \c {baseHeight} (\c {"heightVariation"}, 0 to 120). */
    double heightVariation = 6.0;
    /*! Share of the variation in [0, 1] that comes from sharp ridges instead of rolling hills. */
    double ridges = 0.0;
    /*! Overhangs: a 3D noise of this many blocks reshapes the ground around the surface (ledges,
        arches, overhanging cliffs); 0 keeps plain columns. */
    int overhang = 0;
    /*! Height of terrace steps in blocks (0: none). Where a slowly changing mask allows it, the
        land rises in flat steps with cliffs between them. */
    int terraceHeight = 0;
    /*! Surface block where the ground drops at least \c {steepSlope} blocks to a neighbour (rock
        on cliffs); empty keeps the normal surface. */
    QString steepBlock;
    /*! Drop in blocks to a neighbour from which \c {steepBlock} is used (\c {"steepSlope"}). */
    int steepSlope = 3;
    /*! Top block of the land (\c {"surface"}, required). */
    QString surfaceBlock;
    /*! Block under the surface (\c {"subsurface"}, default \c {surfaceBlock}). */
    QString subsurfaceBlock;
    /*! Blocks of \c {subsurfaceBlock} under the surface before stone (\c {"subsurfaceDepth"},
        0 to 16). */
    int subsurfaceDepth = 3;
    /*! Top block of the ground under the sea level (\c {"underwater"}, default
        \c {subsurfaceBlock}). */
    QString underwaterBlock;
    /*! Patches of other blocks among \c {underwaterBlock} (gravel and clay in river beds,
        decision E-840); their shares add up to at most 1. */
    QList<QcBlockShare> underwaterPatches;
    /*! Patches of other blocks among \c {surfaceBlock} on the land of a delta biome (mud and
        grass on the islands, decision E-850); shares as above. Other biomes ignore them. */
    QList<QcBlockShare> surfacePatches;
    /*! Surface block where land meets the sea, on river banks and on the sand banks of a delta
        (\c {"beach"}, default \c {surfaceBlock}). */
    QString beachBlock;
    /*! Water surface block in cold biomes (e.g. ice); empty keeps water. */
    QString frozenWaterBlock;
    /*! Only water at most this many blocks deep freezes (ice along the banks of a cold delta,
        decision E-850); 0: all water of the biome. */
    int frozenDepth = 0;
    /*! Surface above about this height (a few blocks of jitter) uses \c {snowBlock}
        (\c {"snowLine"}; 0 disables). */
    int snowLine = 0;
    /*! Surface block above \c {snowLine} (\c {"snow"}, required with a snow line). */
    QString snowBlock;
    /*! Villages may be built here (decision E-403), in the style that names the biome or else
        the fallback style. Server side only. */
    bool villages = false;
    /*! A river biome (decision E-840): the climate never chooses it; the water of a river
        channel takes the river biome of the land biome it runs through. Its climate point only
        sets the weather. */
    bool river = false;
    /*! The river biome of the rivers through this land biome (decision E-840; frozen rivers in
        cold biomes); empty: the terrain's \c {riverBiome}. */
    QString riverBiome;
    /*! A delta biome (decision E-850): the climate never chooses it; the arms and islands of a
        river delta take the delta biome of the land biome around them. */
    bool delta = false;
    /*! The delta biome of the deltas in this land biome (decision E-850); empty: the terrain's
        \c {deltaBiome}. */
    QString deltaBiome;
    /*! Monster kinds weighted differently here than by their spawn rules (\c {monsters},
        decision E-838; QcSpawnWeights). Server side only. */
    QList<QcSpawnWeight> monsterWeights;
    /*! The features the biome places, with their densities (\c {"features"}, at most 64). */
    QList<QcFeaturePlacement> features;
    /*! Grass and flowers (decision E-814): placed after the listed features, used by bone meal. */
    QcVegetationDefinition vegetation;
};

/*! \brief A terrain feature (a feature file of a pack): a tree, a column, a single plant or a
    pool of liquid.

    Biomes place trees, columns and plants by density; pools are placed by their own rules. */
struct QcFeatureDefinition
{
    /*! \brief The kind of feature (\c {"type"}: tree, column, plant or pool; default tree). */
    enum class Type {
        Tree, // A trunk of \c {trunkBlock} with a crown of \c {leavesBlock} in \c {shape}.
        Column, // A trunk of \c {trunkBlock} only, without a crown.
        Plant, // A single block (\c {trunkBlock}, given as "block") on the ground.
        /*! A liquid (\c {trunkBlock}, given as "block") filling the bottom of a cave; pools are
            tried in every chunk and are not listed by biomes. */
        Pool
    };

    /*! Qualified name (\c {"name"}). */
    QString name;
    /*! The kind of feature. */
    Type type = Type::Tree;
    /*! Trunk block of trees and columns (\c {"trunk"}), the block of plants and pools
        (\c {"block"}). */
    QString trunkBlock;
    /*! Leaves of a tree's crown (\c {"leaves"}, required for trees). */
    QString leavesBlock;
    /*! Lowest and highest trunk, drawn evenly between them (\c {"minHeight"},
        \c {"maxHeight"}, 1 to 32). \qcshared */
    int minHeight = 4;
    int maxHeight = 6;
    /*! Crown of trees (QcTreeShape::Crown): "round", "cone", "bush", or the giant trees on a 2×2
        trunk "giant_cone" and "giant_round" (decision E-822). */
    QString shape = QStringLiteral("round");
    /*! Vines hung on the trunk and lowest leaves of trees (a block with the state \c {attach});
        empty for none. */
    QString vinesBlock;
    /*! Blocks the feature may stand on; empty allows any solid block. */
    QList<QString> groundBlocks;
    /*! Only on ground at sea level with water right next to it (sugar cane on a shore). */
    bool nearWater = false;
    /*! Plants only: on the surface of the water of columns under the sea level, standing on the
        top water block (lily pads in swamps, decision E-774); \c {ground} names the water. */
    bool onWater = false;
    /*! Size in blocks of the patches the feature grows in (flower meadows, groves); 0 scatters
        it evenly. The average density stays the same. */
    int patchSize = 0;
    /*! Pools: \c {attempts} tries per chunk, each taken with \c {chance}, at a cave floor
        between minY and maxY; a basin of up to \c {radius} blocks is sunk \c {depth} blocks into
        the floor. \qcshared */
    int minY = 8;
    int maxY = 40;
    int attempts = 1;
    double chance = 0.0;
    int radius = 3;
    int depth = 2;
    /*! Pools on the surface of dry land instead of cave floors (lava lakes, decision E-856);
        minY/maxY do not apply. A surface pool is a lake: one per square of \c {region} chunks at
        most, \c {attempts} chunks of the square tried in turn and the first fitting site taken,
        with \c {chance} per square; the tried chunks keep \c {spacing} chunks from the next
        square's. \c {radius} is the basin's reach from its centre, \c {depth} the liquid's;
        liquid next to air or soil turns the soil to \c {rim} (\c {rimBlock}). \qcshared */
    bool surface = false;
    int region = 1;
    int spacing = 0;
    QString rimBlock;
    /*! Biomes a pool may lie in; empty for all. */
    QList<QString> biomes;
};

/*! \brief Ore veins of the terrain generator (an ore file of a pack): compact clumps of
    \c {block} grown inside one chunk, replacing only \c {replaces}. */
struct QcOreDefinition
{
    /*! Qualified name (\c {"name"}). */
    QString name;
    /*! The ore block (\c {"block"}, required). */
    QString block;
    /*! The block a vein replaces (\c {"replaces"}); empty: the terrain's stone block. */
    QString replaces;
    /*! Lowest and highest height of a vein's blocks (\c {"minY"}, \c {"maxY"}). \qcshared */
    int minY = 1;
    int maxY = 64;
    /*! Veins per chunk (\c {"veinsPerChunk"}, 0 to 256). */
    int veinsPerChunk = 8;
    /*! Blocks of a vein at most (\c {"veinSize"}, 1 to 64). */
    int veinSize = 6;
    /*! Biomes a vein may lie in (the biome of the column where it starts); empty for all
        (decision E-453: infested stone only in the mountains). */
    QList<QString> biomes;
};

/*! \brief Soft edges of cold biomes (decision E-858): the top water layer freezes by the
    temperature of its column with a noise instead of by biome, rivers only further into the cold,
    and the ground blocks of a column come from the biome a few blocks away in a jittered direction,
    so they mix over the border.

    Without \c {freezing} in terrain.json the biomes' \c {frozenWater} freezes all their water. */
struct QcColdEdgeSettings
{
    /*! The block still water freezes to where its biome names none; empty: freezing by biome. */
    QString iceBlock;
    /*! Still water (lakes, sea) freezes below this temperature, river water below
        \c {riverTemperature} (\c {"temperature"}, \c {"riverTemperature"}, 0 to 1).
        \qcshared */
    double temperature = 0.0;
    double riverTemperature = 0.0;
    /*! Amplitude of the noise added to the temperature: the ice's edge frays by about this much. */
    double ragged = 0.0;
    /*! Columns take their ground blocks from the biome up to this many blocks away; 0: their
        own. */
    int groundBlend = 0;

    /*! Largest accepted \c {groundBlend}. */
    static constexpr int maxGroundBlend = 8;
};

/*! \brief Settings of the terrain generator (worldgen/terrain.json): sea level, base blocks,
    caves, bedrock, rivers, deltas, the biome scale and the settings of dungeons, mine shafts,
    coastal towers and cold edges.

    A later pack replaces the whole object. Server side only. */
struct QcTerrainSettings
{
    /*! Water fills the columns up to just below this height (\c {"seaLevel"}). */
    int seaLevel = 62;
    /*! The ground's block below the soil (\c {"stone"}, required). */
    QString stoneBlock;
    /*! The sea's block (\c {"water"}, required). */
    QString waterBlock;
    /*! The bottom layers' block (\c {"bedrock"}); empty: \c {stoneBlock}. */
    QString bedrockBlock;
    /*! Whether caves are carved (\c {"caves"}). */
    bool caves = true;
    /*! Larger values make the spaghetti tunnels wider (threshold on two noise fields). */
    double caveTunnelWidth = 0.075;
    /*! Lower values make the open caverns larger (threshold on one noise field). */
    double caveCavernThreshold = 0.58;
    /*! Cave air at or below \c {caveLiquidLevel} is filled with this block (\c {"caveLiquid"};
        lava lakes deep down); an empty block name keeps the caves dry. */
    QString caveLiquidBlock;
    /*! Highest cave cell filled with \c {caveLiquidBlock} (\c {"caveLiquidLevel"}). */
    int caveLiquidLevel = 10;
    /*! Caves break through the ground only where an entrance noise allows it; elsewhere this
        many blocks of rock stay above them. */
    int caveRoof = 4;
    /*! Share of the land, about, where caves may open to the surface. */
    double caveEntrances = 0.04;
    /*! Bedrock: the bottom layer is solid, the layers above it get thinner up to this many
        layers. */
    int bedrockLayers = 3;
    /*! Rivers (decision E-840): winding channels down to the sea level through the land. */
    bool rivers = false;
    /*! The river biome of land biomes that name none (decision E-840); empty: the first river
        biome. */
    QString riverBiome;
    /*! River deltas (decision E-850): where wide rivers run through flat land into the sea; need
        \c {rivers}. */
    bool deltas = false;
    /*! The share of the wide river mouths that get a delta (0..1; decision E-850): deltas are
        rare. */
    double deltaShare = 1.0;
    /*! The delta biome of land biomes that name none (decision E-850); empty: the first delta
        biome. */
    QString deltaBiome;
    /*! Size of the biomes against the content's (decision E-917): the climate maps stretch by
        this factor, so 2 makes biomes and mountain chains about twice as wide; the coasts stay. */
    double biomeScale = 1.0;
    /*! Smallest and largest accepted \c {biomeScale}. \qcshared */
    static constexpr double minBiomeScale = 0.25;
    static constexpr double maxBiomeScale = 8.0;
    /*! Biome whose vegetation bone meal grows where the world generator names no biome (flat,
        showcase and void worlds; decision E-814); empty: none there. */
    QString vegetationBiome;
    /*! Dungeons with spawners and corridors to caves (decision E-522). */
    QcDungeonSettings dungeons;
    /*! Mine shafts with levels, staircases, bridges and surface entrances (decision E-570). */
    QcMineshaftSettings mineshafts;
    /*! Towers on the coasts with a trapped chest at the top (decision E-605). */
    QcCoastalTowerSettings coastalTowers;
    /*! Freezing by temperature and mixed ground at biome borders (decision E-858). */
    QcColdEdgeSettings coldEdges;
};
