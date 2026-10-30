// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"

#include <QList>
#include <QString>

class QcContentRegistry;

/*! \brief Every item of the content, mods included, sorted into the tabs of the creative inventory
    and searchable by name (decision E-286).

    The tabs follow the handbook's categories: blocks, building parts, Signal Dust components
    (decision E-340), tools and armour, food and other items; "all" shows everything, tab after
    tab. Within a tab the order is that of the registry (base game first, then the mods in load
    order). */
class QcCreativeCatalog
{
public:
    /*! The tabs. */
    enum class Category {
        /*! Every item, tab after tab. */
        All,
        /*! Items that place their own block, neither a building part nor a circuit part. */
        Blocks,
        /*! Items that place their own building part (QcBlockDefinition::partOf). */
        Parts,
        /*! Items that place their own Signal Dust component. */
        Signal,
        /*! Tools, armour, igniters and launchers. */
        Tools,
        /*! Food. */
        Food,
        /*! Everything else. */
        Items
    };
    /*! The number of tabs. */
    static constexpr int categoryCount = 7;

    /*! \brief One item of the catalogue. */
    struct Entry
    {
        /*! The item. */
        QcItemId item = qcNoItem;
        /*! Its tab. */
        Category category = Category::Items;
        /*! The name in the catalogue's language. */
        QString label;
        /*! Name, English name and id ("qtcraft:oak_planks", "oak planks") as one search key. */
        QString searchText;
    };

    QcCreativeCatalog() = default;
    /*! The items of \a content with their names in \a language. */
    QcCreativeCatalog(const QcContentRegistry &content, const QString &language);

    /*! Returns the tab of \a item in \a content: its own block (a building part, a circuit
        component or another block), tools and armour, food, else other items (buckets, seeds and
        everything that places a block other than its own). */
    static Category categoryOf(const QcContentRegistry &content, const QcItemDefinition &item);

    /*! Returns every entry in catalogue order. */
    const QList<Entry> &entries() const { return m_entries; }
    /*! Returns the entries of \a category (all of them for All) whose search key contains every
        word of \a query, ignoring case and accents; in catalogue order. */
    QList<const Entry *> filter(Category category, const QString &query) const;

private:
    QList<Entry> m_entries;
};
