// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief Which blocks belong to a showcase category.

    Every given condition must hold; a list holds when any of its entries does. Server side only. */
struct QcShowcaseMatch
{
    /*! Is a liquid. */
    std::optional<bool> liquid;
    /*! A building part made by a part family (stairs, slabs, fences, ...). */
    std::optional<bool> part;
    /*! Gives off block light. */
    std::optional<bool> light;
    /*! Stands on the ground like a plant: support below, ground blocks, sapling or a crossed
        shape. */
    std::optional<bool> plant;
    /*! Can be used: crafting, machine, storage, doors and other toggles. */
    std::optional<bool> functional;
    /*! Is a falling block. */
    std::optional<bool> falls;
    /*! A Signal Dust circuit part (decision E-340): wire, switches, torches, repeaters, lamps,
        pistons. */
    std::optional<bool> signal;
    /*! Render layers ("opaque", "cutout", "translucent"), tools (key \c {"tool"}) and parts of
        the block name without its namespace (\c {"nameContains"}). \qcshared */
    QStringList render;
    QStringList tools;
    QStringList nameContains;
};

/*! \brief One category of the showcase world (an entry of \c {"categories"}).

    \c {name} is its name (lower-case letters, digits and '_'), \c {translationKey} the key of
    its displayed title (\c {"translation"}, default \c {showcase.category.<name>}), \c {match}
    the blocks that belong to it (\c {"match"}). \qcfields */
struct QcShowcaseCategory
{
    QString name;
    QString translationKey;
    QcShowcaseMatch match;
};

/*! \brief Settings of the showcase world (worldgen/showcase.json, decision E-220): the flat ground,
    the time of day, the blocks of podiums, enclosures and item chests, the lookout structure and
    the categories that sort every block of the registry onto podiums.

    A later pack replaces the whole object. */
struct QcShowcaseDefinition
{
    /*! \brief One ground layer: its block (\c {"block"}) and its depth in blocks
        (\c {"depth"}, at least 1). \qcfields */
    struct Layer
    {
        QString block;
        int depth = 1;
    };

    /*! Most entries accepted in \c {"categories"}. */
    static constexpr int maxCategories = 32;
    /*! Most entries accepted in \c {"layers"}. */
    static constexpr int maxLayers = 16;

    /*! Seconds after sunrise (QcDayCycle: 150 s is noon); the time stands still. */
    double timeOfDay = 150.0;
    /*! From the bottom up; the top layer is the ground players walk on. */
    QList<Layer> layers;
    /*! Block of the podiums (\c {"podium"}, default stone). */
    QString podium;
    /*! Block of the enclosures around shown blocks and mob pens (\c {"enclosure"}, default
        glass). */
    QString enclosure;
    /*! Container block of the item chests (\c {"container"}, default chest). */
    QString container;
    /*! Structure placed in front of every area as a lookout; empty for none. */
    QString lookout;
    /*! Areas are put side by side in rows of at most this width (metres). */
    int rowWidth = 160;
    /*! Categories in order; a block goes to the first that matches, else to the last one. */
    QList<QcShowcaseCategory> categories;
};
