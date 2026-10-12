#pragma once

#include "QcItemDefinition.h"

#include <QList>
#include <QString>

class QcContentRegistry;

// Every item of the content, mods included, sorted into the tabs of the creative inventory and
// searchable by name (decision E-286). The tabs follow the handbook's categories: blocks, building
// parts, tools and armour, food and other items; "all" shows everything, tab after tab. Within a
// tab the order is that of the registry (base game first, then the mods in load order).
class QcCreativeCatalog
{
public:
    enum class Category { All, Blocks, Parts, Tools, Food, Items };
    static constexpr int categoryCount = 6;

    struct Entry
    {
        QcItemId item = qcNoItem;
        Category category = Category::Items;
        // The name in the catalogue's language.
        QString label;
        // Name, English name and id ("qtcraft:oak_planks", "oak planks") as one search key.
        QString searchText;
    };

    QcCreativeCatalog() = default;
    // The items of `content` with their names in `language`.
    QcCreativeCatalog(const QcContentRegistry &content, const QString &language);

    // The tab of an item: its own block (a building part or another block), tools and armour, food,
    // else other items (buckets, seeds and everything that places a block other than its own).
    static Category categoryOf(const QcContentRegistry &content, const QcItemDefinition &item);

    const QList<Entry> &entries() const { return m_entries; }
    // The entries of `category` (all of them for All) whose search key contains every word of
    // `query`, ignoring case and accents; in catalogue order.
    QList<const Entry *> filter(Category category, const QString &query) const;

private:
    QList<Entry> m_entries;
};
