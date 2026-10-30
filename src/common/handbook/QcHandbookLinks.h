// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/handbook/QcHandbookEntry.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class QcHandbookText;

/*! \brief Titles, icons and entry ids of content in the handbook, and links to their entries. */
class QcHandbookLinks
{
public:
    /*! A link to an entry. */
    using Link = QcHandbookEntry::Link;

    QcHandbookLinks(const QcContentRegistry &content, const QcHandbookText &text);

    /*! Returns the namespace of the content name \a name (the pack it belongs to); empty without
        one. */
    static QString packOf(const QString &name);
    /*! Returns the content name \a name without its namespace. */
    static QString localPart(const QString &name);
    /*! Returns the local part of \a name as words, the first capitalised ("oak_tree" -> "Oak
        tree"): the last resort for content without a translated name. */
    static QString humanized(const QString &name);
    /*! Returns \a links sorted by text (as compared by the search), then by target. */
    [[nodiscard]] static QList<Link> sortedLinks(QList<Link> links);

    /*! Pack of the overviews: the first loaded pack (the base game). */
    QString basePack() const;
    /*! Tag (without '#') -> member items in content order. */
    const QMap<QString, QStringList> &tagMembers() const { return m_tagMembers; }

    /*! Returns whether the block \a name has an entry (it exists and is not invisible). */
    bool hasBlockEntry(const QString &name) const;
    /*! Returns the entry id of \a item: its block's entry when it has one, else its item entry;
        empty for unknown names. */
    QString itemEntryId(const QString &item) const;
    /*! Returns the translated name of the item or block \a item in \a language; the name itself
        when unknown. */
    QString itemTitle(const QString &item, const QString &language) const;
    /*! Returns \a item when it is an item of the content (its icon), else empty. */
    QString itemIcon(const QString &item) const { return m_content.itemId(item) ? item : QString(); }
    /*! Returns the title of \a tag in \a language; "#" and its local part without a translation. */
    QString tagTitle(const QString &tag, const QString &language) const;
    /*! Returns the title of \a feature in \a language; its humanized name without a translation. */
    QString featureTitle(const QcFeatureDefinition &feature, const QString &language) const;
    /*! Returns the title of \a ore in \a language, made from its block's name. */
    QString oreTitle(const QcOreDefinition &ore, const QString &language) const;

    /*! Returns the link to the entry of \a item with \a note; a liquid shows its bucket. */
    Link itemLink(const QString &item, const QString &note = {}) const;
    /*! Returns the link to the entry of \a block with \a note (as itemLink()). */
    Link blockLink(const QString &block, const QString &note = {}) const { return itemLink(block, note); }
    /*! Returns the link to the entry of \a tag, with the icon of its first member. */
    Link tagLink(const QString &tag) const;
    /*! Returns the link of a recipe's \a ingredient: a tag for "#tag", else an item. */
    Link ingredientLink(const QString &ingredient) const;
    /*! Returns the link to the entry of \a mob with \a note. */
    Link mobLink(const QcMobDefinition &mob, const QString &note = {}) const;
    /*! Returns the link to the entry of \a biome with \a note, with the icon of its surface
        block. */
    Link biomeLink(const QcBiomeDefinition &biome, const QString &note = {}) const;
    /*! Returns the link to the entry of \a feature with \a note, with the icon of its trunk
        block. */
    Link featureLink(const QcFeatureDefinition &feature, const QString &note = {}) const;
    /*! Returns the link to the entry of \a ore with \a note, with the icon of its block. */
    Link oreLink(const QcOreDefinition &ore, const QString &note = {}) const;
    /*! Returns the link to the overview table \a table ("fuels", ...). */
    Link tableLink(const QString &table) const;

private:
    const QcContentRegistry &m_content;
    const QcHandbookText &m_text;
    QMap<QString, QStringList> m_tagMembers;
};
