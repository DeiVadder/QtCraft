// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcContentLoot.h"

#include "common/content/registry/QcContentRegistry.h"

QcLootTables::ItemLookup QcContentLoot::itemsOf(const QcContentRegistry &content)
{
    return [&content](const QString &name) -> std::optional<QcLootTables::ItemInfo> {
        const std::optional<QcItemId> id = content.itemId(name);
        if (!id) {
            return std::nullopt;
        }
        return QcLootTables::ItemInfo{*id, content.item(*id).maxStack};
    };
}

QList<QcItemStack> QcContentLoot::draw(const QcContentRegistry &content, const QString &table, quint64 seed)
{
    const QcLootTableDefinition *definition = content.lootTables().find(table);
    if (!definition) {
        return {};
    }
    return QcLootTables::draw(*definition, seed, itemsOf(content));
}
