#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Which blocks belong to a showcase category. Every given condition must hold; a list holds when any
// of its entries does. Server side only.
struct QcShowcaseMatch
{
    std::optional<bool> liquid;
    // A building part made by a part family (stairs, slabs, fences, ...).
    std::optional<bool> part;
    // Gives off block light.
    std::optional<bool> light;
    // Stands on the ground like a plant: support below, ground blocks, sapling or a crossed shape.
    std::optional<bool> plant;
    // Can be used: crafting, machine, storage, doors and other toggles.
    std::optional<bool> functional;
    std::optional<bool> falls;
    // A Signal Dust circuit part (decision E-340): wire, switches, torches, repeaters, lamps, pistons.
    std::optional<bool> signal;
    // Render layers ("opaque", "cutout", "translucent"), tools and parts of the block name.
    QStringList render;
    QStringList tools;
    QStringList nameContains;
};

struct QcShowcaseCategory
{
    QString name;
    QString translationKey;
    QcShowcaseMatch match;
};

// Settings of the showcase world (worldgen/showcase.json, decision E-220): the flat ground, the time
// of day, the blocks of podiums, enclosures and item chests, the lookout structure and the categories
// that sort every block of the registry onto podiums. A later pack replaces the whole object.
struct QcShowcaseDefinition
{
    struct Layer
    {
        QString block;
        int depth = 1;
    };

    static constexpr int maxCategories = 32;
    static constexpr int maxLayers = 16;

    // Seconds after sunrise (QcDayCycle: 150 s is noon); the time stands still.
    double timeOfDay = 150.0;
    // From the bottom up; the top layer is the ground players walk on.
    QList<Layer> layers;
    QString podium;
    QString enclosure;
    QString container;
    // Structure placed in front of every area as a lookout; empty for none.
    QString lookout;
    // Areas are put side by side in rows of at most this width (metres).
    int rowWidth = 160;
    // Categories in order; a block goes to the first that matches, else to the last one.
    QList<QcShowcaseCategory> categories;
};
