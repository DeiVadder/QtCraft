#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// A building made of blocks, defined in data (structures/*.json, decision E-219): a palette of block
// states and the palette key of every cell, plus what a placed copy brings along: creatures,
// contents of chests and machines, armour stands, labels, block updates to run (liquids start to flow, sand
// falls) and saplings that grow. Coordinates are local: x east, y up, z south, origin at the north
// west bottom corner. Placement turns a structure in quarter turns about the vertical axis. Server
// side only; the showcase world (and later villages) place structures through the world generator.
struct QcStructureDefinition
{
    static constexpr int maxSize = 64;
    static constexpr int maxPaletteEntries = 64;
    // Creatures, contents, labels, updates and growing saplings each.
    static constexpr int maxExtras = 256;
    static constexpr int maxItemsPerContainer = 54;
    // Palette index of a cell that keeps what is there.
    static constexpr qint16 keep = -1;
    // Choices, options of one choice and optional parts each (decision E-404).
    static constexpr int maxChoices = 8;
    static constexpr int maxOptions = 16;
    static constexpr int maxParts = 16;

    struct Mob
    {
        QString mob;
        QcVec3 at;
        // Stays in its enclosure (decision E-460): it neither teleports nor changes the world
        // (decision E-510; the showcase's pens; `"confined": true` in structure data).
        bool confined = false;
        // A tame creature of the owner key (decision E-480; the showcase's pets belong to a made-up
        // owner, "showcase"), sitting unless `sitting` is false; empty: wild.
        QString owner = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
        bool sitting = true;
        // Without a mind, like the reference's NoAI (decision E-510; `"noAi": true`): it stands
        // where it is put.
        bool noAi = false;
    };

    // An armour stand (decision E-238): where it stands, turned by `yaw` degrees (0 faces north),
    // and its properties as command lines ("equip qtcraft:iron", "pose right_arm -90 0 0"), which
    // the server checks when it places the stand (QcStandProperties).
    struct Stand
    {
        QcVec3 at;
        float yaw = 0.0f;
        QStringList properties;
    };

    struct Stack
    {
        QString item;
        int count = 1;
    };

    // Contents of the chest or machine at `at` (machines: input, fuel, output).
    struct Contents
    {
        QcBlockPos at;
        QList<Stack> items;
    };

    // A minecart on the rail at `at`, placed from `item` (a chest or hopper cart takes the `contents`
    // whose `at` is its rail, decision E-386).
    struct Cart
    {
        QcBlockPos at;
        QString item;
    };

    // A monster spawner of the structure (decision E-520): the creature it brings forth and whether
    // its creatures stay put; given to the server with the chunk (QcChunkFurnishing).
    struct Spawner
    {
        QcBlockPos at;
        QString mob;
        bool confined = false;
    };

    // A chest filled from a loot table when its chunk first arrives (decision E-521).
    struct Loot
    {
        QcBlockPos at;
        QString table;
    };

    // A zone of the structure whose guardians come back by themselves (decision E-591; `"guardians"`):
    // while fewer than `max` creatures of `mob` that belong to the zone are awake, at most one comes
    // every `cooldownSeconds` into a cell of the box `from`..`to` (local, both corners inside) with
    // block light at most `maxLight`, when no living player is within `minPlayerDistance` of the box
    // and one is within `maxPlayerDistance` (QcGuardians). The desert temple's sand golem.
    struct Guardian
    {
        QString mob;
        QcBlockPos from;
        QcBlockPos to;
        int max = 1;
        double cooldownSeconds = 120.0;
        double minPlayerDistance = 12.0;
        double maxPlayerDistance = 48.0;
        int maxLight = 7;

        bool operator==(const Guardian &) const = default;
    };

    // Where the terrain generator places the structure by itself (decision E-590; `"surface"`): at
    // most one copy per square region of `spacing` blocks (whole chunks), with `chance`, at the first
    // of `attempts` places that lies wholly inside the region on land above the sea in one of
    // `biomes` (all five samples of its footprint) whose ground varies by at most `maxSlope`;
    // never within `villageMargin` blocks of a village. Its layer `groundLayer` replaces the
    // ground's top block; with `clearTerrain` the terrain above the footprint is cleared from its
    // layer `clearFrom` up (else its keep cells keep the terrain: ruins half in the ground), and
    // `foundation` fills the ground below it down to solid ground. With `waterDepth` its samples may
    // also lie under the sea, at most that deep (a hut on stilts, decision E-622). Its `choices` and
    // `parts` vary each copy (decision E-630).
    struct Surface
    {
        QStringList biomes;
        int spacing = 320;
        double chance = 0.5;
        int attempts = 4;
        int maxSlope = 3;
        int villageMargin = 16;
        QString foundation;
        int waterDepth = 0;
        // Of the places that fit, the first whose middle lies under water rather than the first.
        bool preferWater = false;
        bool clearTerrain = true;
        // The first layer cleared with `clearTerrain`; below it ground and water stay.
        int clearFrom = 1;

        bool operator==(const Surface &) const = default;
    };

    struct Label
    {
        QcVec3 at;
        // Translation key (shown as it is when nothing translates it) and a plain second line.
        QString text;
        QString detail;
        // Titles are larger and seen from further away.
        bool heading = false;
    };

    // A choice of the village planner (decision E-404): one of the options is taken per placed copy,
    // each as likely as the others. An option swaps names (qualified name -> qualified name) in the
    // palette's block names (states keep their properties) and in the creatures' names; an empty
    // option keeps the structure as it is.
    struct Choice
    {
        QString name;
        QList<QHash<QString, QString>> options;
    };

    // An optional part of the village planner (decision E-404): cells in the structure's palette
    // written over its own cells with `chance` per placed copy (keep: the structure's own cell).
    struct Part
    {
        QString name;
        double chance = 0.5;
        // Palette index per cell like `cells`.
        QList<qint16> cells;
    };

    QString name;
    QString translationKey;
    int sizeX = 0;
    int sizeY = 0;
    int sizeZ = 0;
    // Block state names ("qtcraft:torch[attach=north]").
    QStringList palette;
    // Palette index per cell (x + sizeX * (z + sizeZ * y)), or `keep`.
    QList<qint16> cells;
    QList<Mob> mobs;
    QList<Stand> stands;
    QList<Contents> contents;
    QList<Label> labels;
    QList<QcBlockPos> updates;
    QList<QcBlockPos> growing;
    // Minecarts standing on rails when the world is made (decision E-382).
    QList<Cart> minecarts;
    QList<Spawner> spawners;
    QList<Loot> loot;
    // Variation where villages and the terrain generator (surface) place the structure; elsewhere it
    // is placed as it is.
    QList<Choice> choices;
    QList<Part> parts;
    // Shown as a demo in the showcase world, in the order of `showcaseOrder` (then name).
    bool showcase = false;
    int showcaseOrder = 0;
    // A room of the mine shafts with this weight (decision E-570; `"mineshaft": {"weight": 2}`), 0
    // for none: placed at dead ends, its door (the middle of its north side, one block above its
    // bottom layer) towards the corridor.
    int mineshaftWeight = 0;
    // The layer that lies at the ground's top block where the world generator places the structure
    // by itself or in a village (decision E-590): the layers below it are buried (the desert
    // temple's cellar). 0 for structures that stand on the ground.
    int groundLayer = 0;
    // Placed by the terrain generator by itself (decision E-590); std::nullopt: only where data
    // places it (villages, mine shafts, the showcase).
    std::optional<Surface> surface;
    QList<Guardian> guardians;

    qsizetype cellIndex(int x, int y, int z) const { return x + qsizetype(sizeX) * (z + qsizetype(sizeZ) * y); }
};
