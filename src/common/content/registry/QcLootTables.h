#pragma once

#include "common/content/definitions/QcLootTableDefinition.h"
#include "common/gameplay/QcItemStack.h"

#include <QHash>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

// The loot tables of the content (decision E-521), replaced by name like other definitions, and
// what one gives: roll() draws the pools of a table from a seed (the same seed gives the same loot)
// and scatters the stacks over the free slots of a chest. Server side only.
class QcLootTables
{
public:
    // Resolves an item name to its id and largest stack (std::nullopt: unknown, the draw gives nothing).
    struct ItemInfo
    {
        QcItemId id = qcNoItem;
        int maxStack = 64;
    };
    using ItemLookup = std::function<std::optional<ItemInfo>(const QString &name)>;

    void add(QcLootTableDefinition table);
    const QcLootTableDefinition *find(const QString &name) const;
    const QList<QcLootTableDefinition> &all() const { return m_tables; }

    // The stacks a table gives for `seed`, in drawing order (an entry's count beyond its item's
    // stack size is split).
    static QList<QcItemStack> draw(const QcLootTableDefinition &table, quint64 seed, const ItemLookup &items);
    // `stacks` scattered over `slotCount` slots (empty ones stay empty), each to a random free slot drawn
    // from `seed`; stacks beyond the slots are left out.
    static QList<QcItemStack> scatter(const QList<QcItemStack> &stacks, int slotCount, quint64 seed);

private:
    QList<QcLootTableDefinition> m_tables;
    QHash<QString, qsizetype> m_index;
};
