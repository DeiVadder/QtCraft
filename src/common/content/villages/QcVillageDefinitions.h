// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/villages/QcVillagerDefinitions.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Villages in data (decision E-403): the styles (villages/*.json) say which pieces a village of a
// biome is built from and how their blocks are swapped; the settings (worldgen/villages.json) say
// how often and where villages are built, which creature kind lives in them and which golems guard
// them. Names are qualified content names. Server side only (not synchronised).

/*! \brief One blueprint of a kind of building: a structure (structures/\unicode {0x2A}.json)
    whose door side, the south side of its first layer, faces the road it stands on. */
struct QcVillageBlueprint
{
    /*! The structure built (\c {"structure"}). */
    QString structure;
    /*! Relative chance against the other blueprints of the kind. */
    int weight = 1;
};

/*! \brief The walkway of a piece with a water rule (decision E-404): from the end of the road it
    stands at straight on to the water and \c {over} blocks out over it, at the height of the
    road's end. */
struct QcVillageJetty
{
    /*! The walkway's block (\c {deck}) and the posts under its edges (\c {post}; down to the
        ground under the water); oak planks and oak log unless \c {"jetty"} names others.
        \qcshared */
    QString deck;
    QString post;
    /*! Width in blocks: odd, 1 to 5 (an even \c {"width"} becomes the next odd one). */
    int width = 1;
    /*! Blocks the walkway reaches out over the water (\c {"over"}, 1..16). */
    int over = 4;
};

/*! \brief One surface a village may lay its roads of one rank with (decision E-406): \c {block}
    goes into the ground's top block; \c {step} (a slab, optional) is laid on a road cell next to
    one a block higher, so a step on terraced ground reads as a stair.

    A village takes one surface per rank, by \c {weight}. \c {block} and \c {step} may be block
    states. \qcfields */
struct QcVillageRoad
{
    QString block;
    QString step;
    int weight = 1;
};

/*! \brief A crop the fields of a style are planted with (decision E-406): every crop cell of a
    village piece takes the \c {block} of one of these, by \c {weight}, the same along a row of a
    field, ripe or growing. \qcfields */
struct QcVillageCrop
{
    QString block;
    int weight = 1;
};

/*! \brief How a style over water stands (decision E-407): its pieces and boardwalks lie at one
    floor height above the water surface on posts down to the bottom, and stairs lead down where
    a boardwalk ends on dry land. */
struct QcVillageStilts
{
    /*! Floors and boardwalks lie this many blocks above the water surface (the floor block's
        height). */
    int height = 3;
    /*! Posts stand every this many cells under pieces and boardwalks. */
    int spacing = 3;
    /*! The ground lies at most this many blocks below the water surface where a piece or
        boardwalk stands. */
    int maxDepth = 5;
    /*! At least this share of the ground around a centre (the flatness samples) lies under
        water. */
    double water = 0.25;
    /*! The posts (\c {"post"}, spruce log by default). */
    QString post;
    /*! The stairs from a boardwalk's end down to dry land. */
    QString stair;
};

/*! \brief The ground a village over water shapes around itself (decision E-407, E-660, E-661):
    within \c {margin} blocks of its pieces and boardwalks (frayed by noise up to \c {fray} more)
    the ground lies at the water surface, blending back to the terrain over at least \c {blend}
    blocks; on it a noise of \c {scale} blocks forms pools \c {depth} blocks deep over \c {water}
    of the open ground, ringed by \c {mudBand} blocks of the \c {bottom} block, grass between,
    with \c {plant} beside the pools at \c {plantChance} and \c {floatingPlant} on their water at
    \c {floatingPlantChance} (lily pads, decision E-774); dry ground stays where stairs lead down
    to land.

    \c {enabled} is set when the style has a \c {"wetland"} object; the other fields are read
    from its keys of the same names. \qcfields */
struct QcVillageWetland
{
    bool enabled = false;
    int depth = 1;
    int margin = 4;
    int fray = 3;
    int blend = 4;
    QString bottom;
    double water = 0.4;
    int mudBand = 2;
    double scale = 8.0;
    QString plant;
    double plantChance = 0.0;
    QString floatingPlant;
    double floatingPlantChance = 0.0;
};

/*! \brief A range a village draws one value from, uniformly: \c {min} to \c {max}, both
    included. \qcfields */
template<typename Value>
struct QcVillageRange
{
    Value min{};
    Value max{};
};

/*! \brief One street plan the villages of a style are laid out with (decision E-408), chosen per
    village by weight.

    Roads are three blocks wide and run in one of eight directions (diagonals as staircases);
    lengths are shares of the village radius (QcVillageSettings::radius). */
struct QcVillageLayout
{
    /*! The kind of plan (\c {"kind"}: cross, street, t, l, plaza or tree). */
    enum class Kind
    {
        /*! Four roads from the centre, one to each side. */
        Cross,
        /*! One long main road through the centre with side roads off it. */
        Street,
        /*! Three roads from the centre (two opposite, one across). */
        Tee,
        /*! Two roads from the centre at a right angle. */
        Ell,
        /*! A ring road around a square with the centre and a road to it, roads out from the
            ring. */
        Plaza,
        /*! Arms at random angles, each bending once or twice. */
        Tree,
    };

    /*! The kind of plan; its own defaults fill the parameters the data leaves out. */
    Kind kind = Kind::Cross;
    /*! Relative chance against the other layouts of the style (\c {"weight"}, 1..1000). */
    int weight = 1;
    /*! A road from the centre (or from the ring) is this share of the radius long. */
    QcVillageRange<double> armLength{0.55, 1.0};
    /*! Side roads: one candidate spot at 50-70 % of a road from the centre (\c {branchEvery} 0)
        or one every \c {branchEvery} blocks along it, each taken with \c {branchChance},
        \c {branchLength} long (\c {"branches": {"chance", "every", "length"}}). \qcshared */
    double branchChance = 0.6;
    int branchEvery = 0;
    QcVillageRange<double> branchLength{0.3, 0.6};
    /*! Tree: arms from the centre (\c {arms}) and bends per arm (\c {bends}; 45 degrees, 90
        without diagonals). \qcshared */
    QcVillageRange<int> arms{2, 5};
    QcVillageRange<int> bends{1, 2};
    /*! Plaza: blocks from the centre to the ring road's middle line (\c {ring}), and roads out
        from the ring (\c {exits}). \qcshared */
    QcVillageRange<int> ring{9, 13};
    QcVillageRange<int> exits{2, 4};
    /*! Chance per village that its roads wind with the terrain (towards the flatter neighbouring
        direction, at most 45 degrees off their heading); needs diagonals. */
    double winding = 0.0;
    /*! Roads may run diagonally (street, tree); a style over water never does. */
    bool diagonals = true;
};

/*! \brief One kind of building of a style (decision E-403): one of its blueprints, chosen by
    weight, per building.

    The rules (decision E-404, E-408) make a kind conditional: built only with \c {chance} per
    village, only in villages whose roads are at least \c {minRoadLength} blocks long together,
    only at a road end with water at most \c {water} blocks ahead. Kinds with rules are placed
    before the others, at most \c {max} (at least 1). */
struct QcVillagePiece
{
    /*! Most blueprints of a kind. */
    static constexpr int maxBlueprints = 16;

    /*! Its blueprints: \c {"structure"} (one) or \c {"structures"} (1 to maxBlueprints, each
        with a weight). */
    QList<QcVillageBlueprint> blueprints;
    /*! Relative chance against the other kinds of the style. */
    int weight = 1;
    /*! At most this many in one village; 0: no limit. */
    int max = 0;
    /*! Chance per village that it is built at all (\c {"chance"}). */
    double chance = 1.0;
    /*! Least length in blocks of all the village's roads together (\c {"minRoadLength"}). */
    int minRoadLength = 0;
    /*! Blocks ahead of a road end within which water must lie (\c {"water"}, 0..32); 0: no
        water rule. */
    int water = 0;
    /*! The walkway out to the water of a kind with a water rule (\c {"jetty"}). */
    QcVillageJetty jetty;

    /*! Returns whether it has any rule (chance below 1, a least road length or a water rule). */
    bool hasRules() const { return chance < 1.0 || minRoadLength > 0 || water > 0; }
};

/*! \brief Other creatures living in the villages of a style (decision E-421), such as cats:
    \c {mob} with \c {min} to \c {max} of them per village.

    Data only for now: no code spawns them yet (the hook is QcVillagePopulation::spawnVillagers,
    beside the villagers). Read from \c {"residents"} entries with the same keys; \c {min} and
    \c {max} 0..16. \qcfields */
struct QcVillageResident
{
    QString mob;
    int min = 1;
    int max = 1;
};

/*! \brief A village style (villages/\unicode {0x2A}.json, decision E-403): the biomes it is built
    in, the pieces its villages are built from, their roads, fields and street plans, how its
    blocks are swapped and who lives there. */
struct QcVillageStyleDefinition
{
    /*! Most kinds of building of a style. */
    static constexpr int maxPieces = 32;
    /*! Most entries of \c {palette}. */
    static constexpr int maxPaletteEntries = 64;
    /*! Most surfaces of each road rank. */
    static constexpr int maxRoads = 8;
    /*! Most crops of a style. */
    static constexpr int maxCrops = 16;
    /*! Most street plans of a style. */
    static constexpr int maxLayouts = 16;
    /*! Most biomes of a style. */
    static constexpr int maxBiomes = 64;
    /*! Most entries of \c {residents}. */
    static constexpr int maxResidents = 8;

    /*! \brief Where the villages of the style stand: on flat land, or over water on stilts
        (decision E-407). */
    enum class Ground
    {
        Land, // on the ground (\c {"ground": "land"}, the default)
        Water, // over water on stilts (\c {"ground": "water"})
    };

    /*! Content name of the style (\c {"name"}). */
    QString name;
    /*! Biomes built in this style (\c {biomes}). A biome with villages that no style names takes
        the style with \c {fallback} set. \qcshared */
    QStringList biomes;
    bool fallback = false;
    /*! The meeting point in the middle (a well with the bell); its centre lies on the village
        centre. */
    QString centre;
    /*! Taken instead of \c {centre} with \c {rareCentreChance} per village (decision E-592; the
        desert temple in the middle of a desert village;
        \c {"rareCentre": {"structure", "chance"}}), where its larger footprint fits the ground;
        empty: always \c {centre}. \qcshared */
    QString rareCentre;
    double rareCentreChance = 0.0;
    /*! Stands at the road sides every lampSpacing blocks (a lamp post); empty: no lamps. */
    QString lamp;
    /*! The kinds of building (\c {"pieces"}), 1 to maxPieces. */
    QList<QcVillagePiece> pieces;
    /*! Where its villages stand (\c {"ground"}). */
    Ground ground = Ground::Land;
    /*! Posts and stairs of a style over water; unused on land. */
    QcVillageStilts stilts;
    /*! The shaped ground of a style over water; unused on land. */
    QcVillageWetland wetland;
    /*! Surfaces of the roads (decision E-406), one chosen per village and rank: \c {roads} for
        the roads from the centre, \c {branchRoads} for the roads branching off them (default:
        the same list); from \c {"path"} and \c {"branchPath"}, gravel without them. \qcshared */
    QList<QcVillageRoad> roads;
    QList<QcVillageRoad> branchRoads;
    /*! Crops planted in the crop cells of the pieces; empty: the pieces' crops as written. */
    QList<QcVillageCrop> crops;
    /*! Street plans, one chosen per village by weight (decision E-408); never empty after
        parsing (QcVillageLayoutParser::defaultLayouts() without the field). */
    QList<QcVillageLayout> layouts;
    /*! Fills the ground under a piece where the terrain lies lower than its floor. */
    QString foundationBlock;
    /*! Block name -> block name, applied to every piece of the style (states keep their
        properties where the new block has them), so a mod can add a style from the same houses. */
    QHash<QString, QString> palette;
    /*! What its villagers wear (decision E-411); unset: the default look of their species. */
    QcVillagerLook look;
    /*! The creature kind living in its villages (a mob with "villager"); empty: the settings'. */
    QString villager;
    /*! Its golems over the settings' (QcVillageCatalog::golemOfStyle; an empty mob: none). */
    std::optional<QcVillageGolemPatch> golem;
    /*! Other creatures living in its villages (\c {"residents"}). */
    QList<QcVillageResident> residents;
    /*! When the style was last registered (QcVillageCatalog::registerStyle counts up): of two
        styles naming a biome or being the fallback, the later registered one keeps it
        (QcVillageContent::resolveStyles, decision E-421). */
    int registered = 0;
};

/*! \brief The village settings (worldgen/villages.json, decision E-403): how often and where
    villages are built, which creature kind lives in them and which golems guard them.

    A later pack's file is read over the earlier ones'; the JSON keys are the names of the fields
    read. */
struct QcVillageSettings
{
    /*! Edge of the square regions in blocks; each region holds at most one village, always
        inside it. */
    int spacing = 384;
    /*! Chance that a region with suitable ground gets a village. */
    double chance = 0.65;
    /*! Places tried in a region for the centre before the region stays empty. */
    int attempts = 6;
    /*! Pieces stand at most this far from the centre (also the road length). */
    int radius = 48;
    /*! Largest height difference of the ground around the centre. */
    int maxHeightSpread = 6;
    /*! Largest height difference of the ground under one piece. */
    int maxPieceSlope = 3;
    /*! Blocks between two lamps along a road. */
    int lampSpacing = 12;
    /*! Block name -> workstation kind (the name of the profession working there): the village
        state lists these blocks. Filled from the professions (QcContentRegistry::finish), not
        read. */
    QHash<QString, QString> workstations;
    /*! The creature kind living in villages whose style names none (a mob with "villager"). */
    QString villager;
    /*! The golems of villages whose style names none. */
    QcVillageGolemSettings golem;
    /*! The meeting point block (the bell). */
    QString bell;
};
