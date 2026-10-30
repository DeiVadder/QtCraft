// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcAdventureRules.h"
#include "common/gameplay/QcItemStack.h"

#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QHash>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief A building made of blocks, defined in data (structures/\unicode {0x2A}.json, decision
    E-219): a palette of block states and the palette key of every cell, plus what a placed copy
    brings along: creatures, contents of chests and machines, armour stands, labels, block updates
    to run (liquids start to flow, sand falls) and saplings that grow.

    Coordinates are local: x east, y up, z south, origin at the north west bottom corner.
    Placement turns a structure in quarter turns about the vertical axis. Server side only; the
    showcase world (and later villages) place structures through the world generator. */
struct QcStructureDefinition
{
    /*! Most layers, rows per layer and cells per row. */
    static constexpr int maxSize = 64;
    /*! Most palette entries (also the most swaps in one option of a choice). */
    static constexpr int maxPaletteEntries = 64;
    /*! Creatures, contents, labels, updates and growing saplings each. */
    static constexpr int maxExtras = 256;
    /*! Most item stacks in one \c {"contents"} entry. */
    static constexpr int maxItemsPerContainer = 54;
    /*! Palette index of a cell that keeps what is there. */
    static constexpr qint16 keep = -1;
    /*! Most choices, options of one choice and optional parts each (decision E-404). \qcshared */
    static constexpr int maxChoices = 8;
    static constexpr int maxOptions = 16;
    static constexpr int maxParts = 16;

    /*! \brief A creature placed with the structure (an entry of \c {"mobs"}). */
    struct Mob
    {
        /*! Qualified creature name (\c {"mob"}). */
        QString mob;
        /*! Where it is placed (\c {"at"}, a local point at most 4 blocks outside the structure). */
        QcVec3 at;
        /*! Stays in its enclosure (decision E-460): it neither teleports nor changes the world
            (decision E-510; the showcase's pens; \c {"confined": true} in structure data). */
        bool confined = false;
        /*! A tame creature of the owner key (decision E-480; the showcase's pets belong to a
            made-up owner, "showcase"), sitting unless \c {sitting} is false; empty: wild
            (\c {"owner"}, \c {"sitting"}). \qcshared */
        QString owner = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
        bool sitting = true;
        /*! Without a mind, like the reference's NoAI (decision E-510; \c {"noAi": true}): it
            stands where it is put. */
        bool noAi = false;
        /*! Children split off it at once, linked to it (a parent of a family,
            QcMobDefinition::family, decision E-839; \c {"children": 3}; at most the family's
            \c {maxChildren}). They have a mind even when the parent has none (\c {noAi}), so
            they orbit it. */
        int children = 0;
    };

    /*! \brief An armour stand (decision E-238): where it stands, turned by \c {yaw} degrees (0
        faces north), and its properties as command lines ("equip qtcraft:iron", "pose right_arm
        -90 0 0"), which the server checks when it places the stand (QcStandProperties). The JSON
        keys are the field names; a property line keeps at most 256 characters. \qcfields */
    struct Stand
    {
        QcVec3 at;
        float yaw = 0.0f;
        QStringList properties;
    };

    /*! \brief A stack of \c {count} (1 to 64, default 1) \c {item}; \c {name} and
        \c {enchantments} give it a custom name and enchantments (an enchanted book in a showcase
        chest, decision E-873). The JSON keys are the field names. \qcfields */
    struct Stack
    {
        QString item;
        int count = 1;
        QString name = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
        QList<QcEnchantment> enchantments = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    };

    /*! \brief Contents of the chest or machine at \c {at} (machines: input, fuel, output);
        \c {name} gives the chest or machine a custom name (decision E-873), its screen's title;
        \c {items} are the stacks, at most maxItemsPerContainer. The JSON keys are the field names.
        \qcfields */
    struct Contents
    {
        QcBlockPos at;
        QList<Stack> items;
        QString name = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
    };

    /*! \brief A minecart on the rail at \c {at}, placed from \c {item} (a chest or hopper cart
        takes the \c {contents} whose \c {at} is its rail, decision E-386). An entry of
        \c {"minecarts"} is a plain cell for \c {qtcraft:minecart} or an object with \c {"at"} and
        \c {"item"}. \qcfields */
    struct Cart
    {
        QcBlockPos at;
        QString item;
    };

    /*! \brief A boat (decision E-920) with its feet at \c {at} (a point, like a creature's), of
        the boat item \c {item}, pointing to \c {yaw} degrees (0: north). The JSON keys are the
        field names. \qcfields */
    struct Boat
    {
        QcVec3 at;
        QString item;
        float yaw = 0.0f;
    };

    /*! \brief A monster spawner of the structure (decision E-520): the cell \c {at} of the
        spawner, the creature \c {mob} it brings forth and whether its creatures stay put
        (\c {confined}); given to the server with the chunk (QcChunkFurnishing). The JSON keys are
        the field names. \qcfields */
    struct Spawner
    {
        QcBlockPos at;
        QString mob;
        bool confined = false;
    };

    /*! \brief The text on the sign at \c {at} (decision E-766; \c {lines}, up to four of at
        most 32 characters), written when the world is made (the showcase's stations). The JSON keys
        are the field names. \qcfields */
    struct Sign
    {
        QcBlockPos at;
        QStringList lines;
        /*! The colour of the text (QcSignText::Text::color, decision E-830); empty for the
            default. */
        QString color;
    };

    /*! \brief An item frame or painting hung when the world is made (decision E-797, the
        showcase's station): on \c {face} of the block behind cell \c {at} (a painting's lower
        left cell), from \c {item}; a frame holds \c {held} turned by \c {rotation} eighths, a
        painting shows the motif \c {motif}. The JSON keys are the field names. \qcfields */
    struct Hanging
    {
        QcBlockPos at;
        QcBlockFace face = QcBlockFace::North;
        QString item;
        QString held;
        int rotation = 0;
        QString motif;
        /*! A filled map in a frame (decision E-811): which map of a wall map, in maps east and
            south of the map around the first framed map of the world setup; made and revealed
            when the world is made. */
        std::optional<QPoint> map;
    };

    /*! \brief A chest at \c {at} filled from the loot table \c {table} when its chunk first
        arrives (decision E-521). The JSON keys are the field names. \qcfields */
    struct Loot
    {
        QcBlockPos at;
        QString table;
    };

    /*! \brief A zone of the structure whose guardians come back by themselves (decision E-591;
        \c {"guardians"}): while fewer than \c {max} creatures of \c {mob} that belong to the zone
        are awake, at most one comes every \c {cooldownSeconds} into a cell of the box
        \c {from}..\c {to} (local, both corners inside) with block light at most \c {maxLight}, when
        no living player is within \c {minPlayerDistance} of the box and one is within
        \c {maxPlayerDistance} (QcGuardians).

        The desert temple's sand golem. The JSON keys are the field names. \qcfields */
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

    /*! \brief The structure's footprint over the whole height of the world as a spawn area of the
        original game (decision E-871; \c {"spawnArea": {"monsters": [...]}}): a monster that spawns
        naturally in one of its columns is one of \c {monsters}, drawn alike, by the monsters' rules
        of light, ground, distance and cap, one at a time (QcSpawnAreas).

        The witch hut and its witches. \qcfields */
    struct SpawnArea
    {
        QStringList monsters;

        bool operator==(const SpawnArea &) const = default;
    };

    /*! \brief Where the terrain generator places the structure by itself (decision E-590;
        \c {"surface"}): at most one copy per square region of \c {spacing} blocks (whole chunks),
        with \c {chance}, at the first of \c {attempts} places that lies wholly inside the region on
        land above the sea in one of \c {biomes} (all five samples of its footprint) whose ground
        varies by at most \c {maxSlope}; never within \c {villageMargin} blocks of a village.

        Its layer \c {groundLayer} replaces the ground's top block; with \c {clearTerrain} the
        terrain above the footprint is cleared from its layer \c {clearFrom} up (else its keep
        cells keep the terrain: ruins half in the ground), and \c {foundation} fills the ground
        below it down to solid ground. With \c {waterDepth} its samples may also lie under the
        sea, at most that deep (a hut on stilts, decision E-622). Its \c {choices} and \c {parts}
        vary each copy (decision E-630). The JSON keys are the field names. \qcfields */
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
        /*! Of the places that fit, the first whose middle lies under water rather than the
            first. */
        bool preferWater = false;
        /*! Clears the terrain above the footprint from layer \c {clearFrom} up; otherwise the
            keep cells keep the terrain. */
        bool clearTerrain = true;
        /*! The first layer cleared with \c {clearTerrain}; below it ground and water stay. */
        int clearFrom = 1;
        /*! Columns up to this many blocks beside the footprint whose ground is blended to the
            floor (decision E-889): right beside the walls level with it, farther out more and
            more the terrain's own height; 0: none. */
        int blend = 0;
        /*! Trees and plants of the biome grow on the blended ground (decision E-889), all but
            the column right beside the walls; otherwise it stays bare. */
        bool blendVegetation = false;

        bool operator==(const Surface &) const = default;
    };

    /*! \brief A floating text placed with the structure (an entry of \c {"labels"}). */
    struct Label
    {
        /*! Where it floats (\c {"at"}, a local point at most 4 blocks outside the structure). */
        QcVec3 at;
        /*! Translation key (shown as it is when nothing translates it) and a plain second line
            (\c {"text"}, required, and \c {"detail"}; at most 256 characters each). \qcshared */
        QString text;
        QString detail;
        /*! Titles are larger and seen from further away. */
        bool heading = false;
    };

    /*! \brief A choice of the village planner (decision E-404): one of the options is taken per
        placed copy, each as likely as the others.

        An option swaps names (qualified name -> qualified name) in the palette's block names
        (states keep their properties) and in the creatures' names; an empty option keeps the
        structure as it is. \c {name} is the choice's key in \c {"choices"}, \c {options} its
        options (1 to maxOptions). \qcfields */
    struct Choice
    {
        QString name;
        QList<QHash<QString, QString>> options;
    };

    /*! \brief An optional part of the village planner (decision E-404): cells in the structure's
        palette written over its own cells with \c {chance} per placed copy (keep: the
        structure's own cell). */
    struct Part
    {
        /*! The part's name (\c {"name"}). */
        QString name;
        /*! Chance per placed copy (\c {"chance"}, 0 to 1). */
        double chance = 0.5;
        /*! Palette index per cell like \c {cells}. */
        QList<qint16> cells;
    };

    /*! Qualified name (\c {"name"}). */
    QString name;
    /*! Translation key of its name (\c {"translation"}, default made from the name with the
        prefix \c {structure}, QcContentFields::translationKey()). */
    QString translationKey;
    /*! Size in cells along x (the length of a row of \c {"layers"}). */
    int sizeX = 0;
    /*! Size in cells along y (the number of layers). */
    int sizeY = 0;
    /*! Size in cells along z (the rows per layer). */
    int sizeZ = 0;
    /*! Block state names ("qtcraft:torch[attach=north]"). */
    QStringList palette;
    /*! Palette index per cell (x + sizeX * (z + sizeZ * y)), or \c {keep}. */
    QList<qint16> cells;
    /*! Creatures placed with it (\c {"mobs"}). */
    QList<Mob> mobs;
    /*! Armour stands (\c {"stands"}). */
    QList<Stand> stands;
    /*! Contents of chests and machines (\c {"contents"}). */
    QList<Contents> contents;
    /*! Floating texts (\c {"labels"}). */
    QList<Label> labels;
    /*! Cells whose block update runs after placing (\c {"updates"}): liquids start to flow,
        sand falls. */
    QList<QcBlockPos> updates;
    /*! Cells of saplings that grow after placing (\c {"growing"}). */
    QList<QcBlockPos> growing;
    /*! Minecarts standing on rails when the world is made (decision E-382). */
    QList<Cart> minecarts;
    /*! Boats lying on water or ground when the world is made (decision E-920). */
    QList<Boat> boats;
    /*! Monster spawners (\c {"spawners"}). */
    QList<Spawner> spawners;
    /*! Chests filled from loot tables (\c {"loot"}). */
    QList<Loot> loot;
    /*! Texts of signs (\c {"signs"}). */
    QList<Sign> signs;
    /*! Item frames and paintings (\c {"hangings"}); only showcase structures may have them. */
    QList<Hanging> hangings;
    /*! Variation where villages and the terrain generator (surface) place the structure;
        elsewhere it is placed as it is (\c {"choices"}, \c {"parts"}). \qcshared */
    QList<Choice> choices;
    QList<Part> parts;
    /*! Shown as a demo in the showcase world, in the order of \c {showcaseOrder} (then name):
        a \c {"showcase"} object with its \c {"order"}. \qcshared */
    bool showcase = false;
    int showcaseOrder = 0;
    /*! The showcase tour looks at it from in front of its south side at eye height (a station
        behind glass, decision E-839; \c {"showcase": {"front": true}}) instead of from the
        lookout. */
    bool showcaseFromFront = false;
    /*! A room of the mine shafts with this weight (decision E-570;
        \c {"mineshaft": {"weight": 2}}), 0 for none: placed at dead ends, its door (the middle
        of its north side, one block above its bottom layer) towards the corridor. */
    int mineshaftWeight = 0;
    /*! The layer that lies at the ground's top block where the world generator places the
        structure by itself or in a village (decision E-590): the layers below it are buried (the
        desert temple's cellar). 0 for structures that stand on the ground. */
    int groundLayer = 0;
    /*! Placed by the terrain generator by itself (decision E-590); std::nullopt: only where data
        places it (villages, mine shafts, the showcase). */
    std::optional<Surface> surface;
    /*! Zones whose guardians come back by themselves (\c {"guardians"}, decision E-591). */
    QList<Guardian> guardians;
    /*! The footprint as a spawn area of its own monsters (\c {"spawnArea"}, decision E-871);
        none: an ordinary area. */
    std::optional<SpawnArea> spawnArea;
    /*! Allow-list entries of adventure mode (decision E-735) the showcase adds to the world when
        it places the structure (\c {"adventure": {"canBreak": {item: [blocks]}, "canPlaceOn":
        {…}}}); like every allow-list they hold in the whole world. */
    QcAdventureRules adventure;

    /*! Returns the index in \c {cells} of the cell at \a x, \a y, \a z. */
    qsizetype cellIndex(int x, int y, int z) const { return x + static_cast<qsizetype>(sizeX) * (z + static_cast<qsizetype>(sizeZ) * y); }
};
