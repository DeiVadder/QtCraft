// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/recipes/QcLootTables.h"

#include <QRandomGenerator>

#include <algorithm>

void QcLootTables::add(QcLootTableDefinition table)
{
    const auto existing = m_index.constFind(table.name);
    if (existing != m_index.constEnd()) {
        m_tables[*existing] = std::move(table);
    } else {
        m_index.insert(table.name, m_tables.size());
        m_tables.append(std::move(table));
    }
}

const QcLootTableDefinition *QcLootTables::find(const QString &name) const
{
    const auto it = m_index.constFind(name);
    return it == m_index.constEnd() ? nullptr : &m_tables[*it];
}

QList<QcItemStack> QcLootTables::draw(const QcLootTableDefinition &table, quint64 seed, const ItemLookup &items)
{
    QRandomGenerator random(static_cast<quint32>(seed ^ (seed >> 32)));
    QList<QcItemStack> stacks;
    for (const QcLootTableDefinition::Pool &pool : table.pools) {
        qint64 total = 0;
        for (const QcLootTableDefinition::Entry &entry : pool.entries) {
            total += entry.weight;
        }
        const int rolls = pool.minRolls + static_cast<int>(random.bounded(static_cast<quint32>(pool.maxRolls - pool.minRolls + 1)));
        for (int roll = 0; roll < rolls && total > 0; ++roll) {
            qint64 pick = static_cast<qint64>(random.bounded(static_cast<quint64>(total)));
            const auto chosen = std::ranges::find_if(pool.entries, [&pick](const QcLootTableDefinition::Entry &entry) {
                pick -= entry.weight;
                return pick < 0;
            });
            const int count = chosen->minCount + static_cast<int>(random.bounded(static_cast<quint32>(chosen->maxCount - chosen->minCount + 1)));
            const std::optional<ItemInfo> item = chosen->item.isEmpty() ? std::nullopt : items(chosen->item);
            for (int left = count; item && left > 0;) {
                const int size = std::min(left, std::max(1, item->maxStack));
                stacks.append({item->id, size, 0});
                left -= size;
            }
        }
    }
    return stacks;
}

QList<QcItemStack> QcLootTables::scatter(const QList<QcItemStack> &stacks, int slotCount, quint64 seed)
{
    QList<QcItemStack> chest(std::max<qsizetype>(0, slotCount));
    QList<int> free;
    free.reserve(chest.size());
    for (int slot = 0; slot < static_cast<int>(chest.size()); ++slot) {
        free.append(slot);
    }
    QRandomGenerator random(static_cast<quint32>((seed >> 32) ^ seed ^ 0x9e3779b9u));
    for (const QcItemStack &stack : stacks) {
        if (free.isEmpty()) {
            break;
        }
        const qsizetype at = static_cast<qsizetype>(random.bounded(static_cast<quint32>(free.size())));
        chest[free[at]] = stack;
        free.removeAt(at);
    }
    return chest;
}
