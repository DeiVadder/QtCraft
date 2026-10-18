#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

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
    // Variation where villages place the structure; elsewhere it is placed as it is.
    QList<Choice> choices;
    QList<Part> parts;
    // Shown as a demo in the showcase world, in the order of `showcaseOrder` (then name).
    bool showcase = false;
    int showcaseOrder = 0;

    qsizetype cellIndex(int x, int y, int z) const { return x + qsizetype(sizeX) * (z + qsizetype(sizeZ) * y); }
};
