#pragma once

#include "common/content/definitions/QcVillagerDefinitions.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Villages in data (decision E-403): the styles (villages/*.json) say which pieces a village of a
// biome is built from and how their blocks are swapped; the settings (worldgen/villages.json) say
// how often and where villages are built, which creature kind lives in them and which golems guard
// them. Names are qualified content names. Server side only (not synchronised).

// One blueprint of a kind of building: a structure (structures/*.json) whose door side, the south
// side of its first layer, faces the road it stands on.
struct QcVillageBlueprint
{
    QString structure;
    // Relative chance against the other blueprints of the kind.
    int weight = 1;
};

// The walkway of a piece with a water rule (decision E-404): from the end of the road it stands at
// straight on to the water and `over` blocks out over it, at the height of the road's end.
struct QcVillageJetty
{
    // The walkway's block and the posts under its edges (down to the ground under the water).
    QString deck;
    QString post;
    // Odd, 1 to 5.
    int width = 1;
    int over = 4;
};

// One surface a village may lay its roads of one rank with (decision E-406): `block` goes into the
// ground's top block; `step` (a slab, optional) is laid on a road cell next to one a block higher,
// so a step on terraced ground reads as a stair. A village takes one surface per rank, by weight.
struct QcVillageRoad
{
    QString block;
    QString step;
    int weight = 1;
};

// A crop the fields of a style are planted with (decision E-406): every crop cell of a village piece
// takes one of these, by weight, the same along a row of a field, ripe or growing.
struct QcVillageCrop
{
    QString block;
    int weight = 1;
};

// How a style over water stands (decision E-407): its pieces and boardwalks lie at one floor height
// above the water surface on posts down to the bottom, and stairs lead down where a boardwalk ends
// on dry land.
struct QcVillageStilts
{
    // Floors and boardwalks lie this many blocks above the water surface (the floor block's height).
    int height = 3;
    // Posts stand every this many cells under pieces and boardwalks.
    int spacing = 3;
    // The ground lies at most this many blocks below the water surface where a piece or boardwalk stands.
    int maxDepth = 5;
    // At least this share of the ground around a centre (the flatness samples) lies under water.
    double water = 0.25;
    QString post;
    // The stairs from a boardwalk's end down to dry land.
    QString stair;
};

// The ground a village over water shapes around itself (decision E-407): within `margin` blocks of
// its pieces and boardwalks (frayed by noise up to `fray` more) the ground lies `depth` blocks below
// the water surface with a `bottom` of mud, blending back to the terrain over `blend` blocks; dry
// ground stays where stairs lead down to land.
struct QcVillageWetland
{
    bool enabled = false;
    int depth = 1;
    int margin = 4;
    int fray = 3;
    int blend = 4;
    QString bottom;
};

// A range a village draws one value from, uniformly (inclusive).
template<typename Value>
struct QcVillageRange
{
    Value min{};
    Value max{};
};

// One street plan the villages of a style are laid out with (decision E-408), chosen per village by
// weight. Roads are three blocks wide and run in one of eight directions (diagonals as staircases);
// lengths are shares of the village radius (QcVillageSettings::radius).
struct QcVillageLayout
{
    enum class Kind
    {
        // Four roads from the centre, one to each side.
        Cross,
        // One long main road through the centre with side roads off it.
        Street,
        // Three roads from the centre (two opposite, one across) or two at a right angle.
        Tee,
        Ell,
        // A ring road around a square with the centre and a road to it, roads out from the ring.
        Plaza,
        // Arms at random angles, each bending once or twice.
        Tree,
    };

    Kind kind = Kind::Cross;
    int weight = 1;
    // A road from the centre (or from the ring) is this share of the radius long.
    QcVillageRange<double> armLength{0.55, 1.0};
    // Side roads: one candidate spot at 50-70 % of a road from the centre (`branchEvery` 0) or one
    // every `branchEvery` blocks along it, each taken with `branchChance`, `branchLength` long.
    double branchChance = 0.6;
    int branchEvery = 0;
    QcVillageRange<double> branchLength{0.3, 0.6};
    // Tree: arms from the centre and bends per arm (45 degrees, 90 without diagonals).
    QcVillageRange<int> arms{2, 5};
    QcVillageRange<int> bends{1, 2};
    // Plaza: blocks from the centre to the ring road's middle line, and roads out from the ring.
    QcVillageRange<int> ring{9, 13};
    QcVillageRange<int> exits{2, 4};
    // Chance per village that its roads wind with the terrain (towards the flatter neighbouring
    // direction, at most 45 degrees off their heading); needs diagonals.
    double winding = 0.0;
    // Roads may run diagonally (street, tree); a style over water never does.
    bool diagonals = true;
};

// One kind of building of a style (decision E-403): one of its blueprints, chosen by weight, per
// building. The rules (decision E-404, E-408) make a kind conditional: built only with `chance` per
// village, only in villages whose roads are at least `minRoadLength` blocks long together, only at
// a road end with water at most `water` blocks ahead. Kinds with rules are placed before the
// others, at most `max` (at least 1).
struct QcVillagePiece
{
    static constexpr int maxBlueprints = 16;

    QList<QcVillageBlueprint> blueprints;
    // Relative chance against the other kinds of the style.
    int weight = 1;
    // At most this many in one village; 0: no limit.
    int max = 0;
    double chance = 1.0;
    int minRoadLength = 0;
    // 0: no water rule.
    int water = 0;
    QcVillageJetty jetty;

    bool hasRules() const { return chance < 1.0 || minRoadLength > 0 || water > 0; }
};

// Other creatures living in the villages of a style (decision E-421), such as cats: `mob` with
// `min` to `max` of them per village. Data only for now: no code spawns them yet (the hook is
// QcVillagePopulation::spawnVillagers, beside the villagers).
struct QcVillageResident
{
    QString mob;
    int min = 1;
    int max = 1;
};

struct QcVillageStyleDefinition
{
    static constexpr int maxPieces = 32;
    static constexpr int maxPaletteEntries = 64;
    static constexpr int maxRoads = 8;
    static constexpr int maxCrops = 16;
    static constexpr int maxLayouts = 16;
    static constexpr int maxBiomes = 64;
    static constexpr int maxResidents = 8;

    // Where the villages of the style stand: on flat land, or over water on stilts (decision E-407).
    enum class Ground
    {
        Land,
        Water,
    };

    QString name;
    // Biomes built in this style. A biome with villages that no style names takes the fallback.
    QStringList biomes;
    bool fallback = false;
    // The meeting point in the middle (a well with the bell); its centre lies on the village centre.
    QString centre;
    // Taken instead of `centre` with `rareCentreChance` per village (decision E-592; the desert
    // temple in the middle of a desert village; `"rareCentre": {"structure", "chance"}`), where its
    // larger footprint fits the ground; empty: always `centre`.
    QString rareCentre;
    double rareCentreChance = 0.0;
    // Stands at the road sides every lampSpacing blocks (a lamp post); empty: no lamps.
    QString lamp;
    QList<QcVillagePiece> pieces;
    Ground ground = Ground::Land;
    // Posts and stairs of a style over water; unused on land.
    QcVillageStilts stilts;
    // The shaped ground of a style over water; unused on land.
    QcVillageWetland wetland;
    // Surfaces of the roads (decision E-406), one chosen per village and rank: `roads` for the roads
    // from the centre, `branchRoads` for the roads branching off them (default: the same list).
    QList<QcVillageRoad> roads;
    QList<QcVillageRoad> branchRoads;
    // Crops planted in the crop cells of the pieces; empty: the pieces' crops as written.
    QList<QcVillageCrop> crops;
    // Street plans, one chosen per village by weight (decision E-408); never empty after parsing
    // (QcVillageParser::defaultLayouts() without the field).
    QList<QcVillageLayout> layouts;
    // Fills the ground under a piece where the terrain lies lower than its floor.
    QString foundationBlock;
    // Block name -> block name, applied to every piece of the style (states keep their properties
    // where the new block has them), so a mod can add a style from the same houses.
    QHash<QString, QString> palette;
    // What its villagers wear (decision E-411); unset: the default look of their species.
    QcVillagerLook look;
    // The creature kind living in its villages (a mob with "villager"); empty: the settings'.
    QString villager;
    // Its golems over the settings' (QcContentRegistry::golemOfStyle; an empty mob: none).
    std::optional<QcVillageGolemPatch> golem;
    QList<QcVillageResident> residents;
    // When the style was last registered (QcContentRegistry::registerVillageStyle counts up): of two
    // styles naming a biome or being the fallback, the later registered one keeps it
    // (QcVillageContent::resolveStyles, decision E-421).
    int registered = 0;
};

struct QcVillageSettings
{
    // Edge of the square regions in blocks; each region holds at most one village, always inside it.
    int spacing = 384;
    // Chance that a region with suitable ground gets a village.
    double chance = 0.65;
    // Places tried in a region for the centre before the region stays empty.
    int attempts = 6;
    // Pieces stand at most this far from the centre (also the road length).
    int radius = 48;
    // Largest height difference of the ground around the centre.
    int maxHeightSpread = 6;
    // Largest height difference of the ground under one piece.
    int maxPieceSlope = 3;
    // Blocks between two lamps along a road.
    int lampSpacing = 12;
    // Block name -> workstation kind (the name of the profession working there): the village state
    // lists these blocks. Filled from the professions (QcContentRegistry::finish), not read.
    QHash<QString, QString> workstations;
    // The creature kind living in villages whose style names none (a mob with "villager").
    QString villager;
    // The golems of villages whose style names none.
    QcVillageGolemSettings golem;
    // The meeting point block (the bell).
    QString bell;
};
