// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/recipes/QcLootTableDefinition.h"
#include "common/gameplay/QcItemStack.h"

#include <QHash>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

/*! \brief The loot tables of the content (decision E-521), replaced by name like other definitions,
    and what one gives: roll() draws the pools of a table from a seed (the same seed gives the same
    loot) and scatters the stacks over the free slots of a chest.

    Server side only. */
class QcLootTables
{
public:
    /*! \brief What draw() needs to know of an item: its id and largest stack. */
    struct ItemInfo
    {
        /*! The item's id. */
        QcItemId id = qcNoItem;
        /*! Most items in one of its stacks. */
        int maxStack = 64;
    };
    /*! Resolves an item name to its ItemInfo (std::nullopt: unknown, the draw gives nothing). */
    using ItemLookup = std::function<std::optional<ItemInfo>(const QString &name)>;

    /*! Adds \a table, replacing a table of the same name in its place. */
    void add(QcLootTableDefinition table);
    /*! Returns the table named \a name, or \c nullptr when there is none. */
    const QcLootTableDefinition *find(const QString &name) const;
    /*! Returns all tables in the order they were first added. */
    const QList<QcLootTableDefinition> &all() const { return m_tables; }

    /*! Returns the stacks \a table gives for \a seed, in drawing order, with item names resolved
        by \a items (an entry's count beyond its item's stack size is split). */
    static QList<QcItemStack> draw(const QcLootTableDefinition &table, quint64 seed, const ItemLookup &items);
    /*! Returns \a stacks scattered over \a slotCount slots (empty ones stay empty), each to a
        random free slot drawn from \a seed; stacks beyond the slots are left out. */
    static QList<QcItemStack> scatter(const QList<QcItemStack> &stacks, int slotCount, quint64 seed);

private:
    QList<QcLootTableDefinition> m_tables;
    QHash<QString, qsizetype> m_index;
};
