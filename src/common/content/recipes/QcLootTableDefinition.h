// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

/*! \brief A loot table (loot/<name>.json, decision E-521): what a chest the world generator places
    holds (dungeons; later mine shafts and surface structures).

    Each pool draws a number of times between \c {minRolls} and \c {maxRolls}, each draw one
    entry by weight; an entry without an item stands for nothing. Server side only. */
struct QcLootTableDefinition
{
    /*! \brief One entry of a pool: an item and how many of it, drawn by weight. */
    struct Entry
    {
        /*! Content name of the item; empty: the draw gives nothing. */
        QString item;
        /*! Relative chance against the other entries of the pool (\c {"weight"}, 0 to
            maxWeight, default 1). */
        int weight = 1;
        /*! Fewest items a draw gives (\c {"count"}, a number or the first of [min, max]). */
        int minCount = 1;
        /*! Most items a draw gives (\c {"count"}, the number or the second of [min, max]). */
        int maxCount = 1;
    };

    /*! \brief A pool of entries drawn from a number of times. */
    struct Pool
    {
        /*! Fewest draws (\c {"rolls"}, a number or the first of [min, max], 0 to maxRolls). */
        int minRolls = 1;
        /*! Most draws (\c {"rolls"}, the number or the second of [min, max]). */
        int maxRolls = 1;
        /*! The entries (\c {"entries"}, 1 to maxEntries). */
        QList<Entry> entries;
    };

    /*! Upper limit of a pool's rolls. */
    static constexpr int maxRolls = 64;
    /*! Upper limit of an entry's weight. */
    static constexpr int maxWeight = 1000000;
    /*! Most pools of a table. */
    static constexpr int maxPools = 16;
    /*! Most entries of a pool. */
    static constexpr int maxEntries = 256;

    /*! Qualified name (\c {"name"}, required). */
    QString name;
    /*! The pools, each drawn in turn (\c {"pools"}, 1 to maxPools). */
    QList<Pool> pools;
};
