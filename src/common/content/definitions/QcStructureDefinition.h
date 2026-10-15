#pragma once

#include "QcBlockPos.h"
#include "QcVec3.h"

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

    struct Mob
    {
        QString mob;
        QcVec3 at;
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
    // Shown as a demo in the showcase world, in the order of `showcaseOrder` (then name).
    bool showcase = false;
    int showcaseOrder = 0;

    qsizetype cellIndex(int x, int y, int z) const { return x + qsizetype(sizeX) * (z + qsizetype(sizeZ) * y); }
};
