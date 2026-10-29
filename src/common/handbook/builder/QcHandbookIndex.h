// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/handbook/QcHandbookEntry.h"
#include "common/handbook/QcLinkList.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

class QcHandbookLinks;
class QcHandbookText;

// Reverse lookups over the content for the handbook pages, all in content order: recipes by result
// and ingredient, smelting by result and input, what drops an item, where a block occurs, which
// creatures spawn on it, machines by category and parts by base block.
class QcHandbookIndex
{
public:
    using Link = QcHandbookEntry::Link;
    // A recipe index with the tag the item fits through (empty when named directly).
    using IngredientUse = std::pair<qsizetype, QString>;

    // Reports recipes with tags no item has to `text`.
    QcHandbookIndex(const QcContentRegistry &content, QcHandbookText &text, const QcHandbookLinks &links);

    QList<qsizetype> recipesByResult(const QString &item) const { return m_recipesByResult.value(item); }
    QList<IngredientUse> recipesByIngredient(const QString &item) const { return m_recipesByIngredient.value(item); }
    QList<qsizetype> smeltingByResult(const QString &item) const { return m_smeltingByResult.value(item); }
    QList<qsizetype> smeltingByInput(const QString &item) const { return m_smeltingByInput.value(item); }
    QList<Link> droppedBy(const QString &item) const { return m_droppedBy.value(item).links(); }
    // Biomes, ore veins and pools a block occurs in.
    QList<Link> foundIn(const QString &block) const { return m_foundIn.value(block).links(); }
    QList<Link> spawnersOn(const QString &block) const { return m_spawnersOn.value(block).links(); }
    // Indices into the content's blocks: the machines and campfires that work on a recipe category.
    QList<qsizetype> machinesByCategory(const QString &category) const { return m_machinesByCategory.value(category); }
    QStringList partsByBase(const QString &block) const { return m_partsByBase.value(block); }

private:
    void indexRecipes(QcHandbookText &text, const QcHandbookLinks &links);
    void indexBlocks(const QcHandbookText &text, const QcHandbookLinks &links);
    void indexMobs(const QcHandbookText &text, const QcHandbookLinks &links);
    void indexOccurrences(const QcHandbookText &text, const QcHandbookLinks &links);
    void indexBiome(const QcBiomeDefinition &biome, const QcHandbookText &text, const QcHandbookLinks &links);

    const QcContentRegistry &m_content;
    QHash<QString, QList<qsizetype>> m_recipesByResult;
    QHash<QString, QList<IngredientUse>> m_recipesByIngredient;
    QHash<QString, QList<qsizetype>> m_smeltingByResult;
    QHash<QString, QList<qsizetype>> m_smeltingByInput;
    QHash<QString, QcLinkList> m_droppedBy;
    QHash<QString, QcLinkList> m_foundIn;
    QHash<QString, QcLinkList> m_spawnersOn;
    QHash<QString, QList<qsizetype>> m_machinesByCategory;
    QHash<QString, QStringList> m_partsByBase;
};
