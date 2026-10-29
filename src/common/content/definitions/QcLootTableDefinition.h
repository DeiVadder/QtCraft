// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

// A loot table (loot/<name>.json, decision E-521): what a chest the world generator places holds
// (dungeons; later mine shafts and surface structures). Each pool draws a number of times between
// `minRolls` and `maxRolls`, each draw one entry by weight; an entry without an item stands for
// nothing. Server side only.
struct QcLootTableDefinition
{
    struct Entry
    {
        // Content name of the item; empty: the draw gives nothing.
        QString item;
        int weight = 1;
        int minCount = 1;
        int maxCount = 1;
    };

    struct Pool
    {
        int minRolls = 1;
        int maxRolls = 1;
        QList<Entry> entries;
    };

    static constexpr int maxRolls = 64;
    static constexpr int maxWeight = 1000000;
    static constexpr int maxPools = 16;
    static constexpr int maxEntries = 256;

    QString name;
    QList<Pool> pools;
};
