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

/*! \brief Reverse lookups over the content for the handbook pages, all in content order: recipes
    by result and ingredient, smelting by result and input, what drops an item, where a block
    occurs, which creatures spawn on it, machines by category and parts by base block. */
class QcHandbookIndex
{
public:
    /*! A link to an entry. */
    using Link = QcHandbookEntry::Link;
    /*! A recipe index with the tag the item fits through (empty when named directly). */
    using IngredientUse = std::pair<qsizetype, QString>;

    /*! Builds the lookups over \a content, with the titles and links of \a links. Reports recipes
        with tags no item has to \a text. */
    QcHandbookIndex(const QcContentRegistry &content, QcHandbookText &text, const QcHandbookLinks &links);

    /*! Returns the indices of the crafting recipes that make \a item. */
    QList<qsizetype> recipesByResult(const QString &item) const { return m_recipesByResult.value(item); }
    /*! Returns the crafting recipes that take \a item, by name or through a tag. */
    QList<IngredientUse> recipesByIngredient(const QString &item) const { return m_recipesByIngredient.value(item); }
    /*! Returns the indices of the smelting recipes that make \a item. */
    QList<qsizetype> smeltingByResult(const QString &item) const { return m_smeltingByResult.value(item); }
    /*! Returns the indices of the smelting recipes that take \a item. */
    QList<qsizetype> smeltingByInput(const QString &item) const { return m_smeltingByInput.value(item); }
    /*! Returns links to the blocks and creatures that drop \a item, with the drop as note. */
    QList<Link> droppedBy(const QString &item) const { return m_droppedBy.value(item).links(); }
    /*! Returns links to the biomes, ore veins and pools \a block occurs in. */
    QList<Link> foundIn(const QString &block) const { return m_foundIn.value(block).links(); }
    /*! Returns links to the creatures that spawn on \a block. */
    QList<Link> spawnersOn(const QString &block) const { return m_spawnersOn.value(block).links(); }
    /*! Returns indices into the content's blocks: the machines and campfires that work on the
        recipe \a category. */
    QList<qsizetype> machinesByCategory(const QString &category) const { return m_machinesByCategory.value(category); }
    /*! Returns the building parts made of the material whose base block is \a block. */
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
