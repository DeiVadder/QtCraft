#pragma once

#include "QcContentRegistry.h"
#include "QcHandbookEntry.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class QcHandbookText;

// Titles, icons and entry ids of content in the handbook, and links to their entries.
class QcHandbookLinks
{
public:
    using Link = QcHandbookEntry::Link;

    QcHandbookLinks(const QcContentRegistry &content, const QcHandbookText &text);

    // Namespace of a content name (the pack it belongs to); empty without one.
    static QString packOf(const QString &name);
    static QString localPart(const QString &name);
    // "oak_tree" -> "Oak tree": the last resort for content without a translated name.
    static QString humanized(const QString &name);
    // Sorts by text (as compared by the search), then by target.
    static void sortLinks(QList<Link> &links);
    // Appends the link unless one to the same target exists; that one gets the note added.
    static void appendUnique(QList<Link> &links, Link link);

    // Pack of the overviews: the first loaded pack (the base game).
    QString basePack() const;
    // Tag (without '#') -> member items in content order.
    const QMap<QString, QStringList> &tagMembers() const { return m_tagMembers; }

    bool hasBlockEntry(const QString &name) const;
    QString itemEntryId(const QString &item) const;
    QString itemTitle(const QString &item, const QString &language) const;
    QString itemIcon(const QString &item) const { return m_content.itemId(item) ? item : QString(); }
    QString tagTitle(const QString &tag, const QString &language) const;
    QString featureTitle(const QcFeatureDefinition &feature, const QString &language) const;
    QString oreTitle(const QcOreDefinition &ore, const QString &language) const;

    Link itemLink(const QString &item, const QString &note = {}) const;
    Link blockLink(const QString &block, const QString &note = {}) const { return itemLink(block, note); }
    Link tagLink(const QString &tag) const;
    Link ingredientLink(const QString &ingredient) const;
    Link mobLink(const QcMobDefinition &mob, const QString &note = {}) const;
    Link biomeLink(const QcBiomeDefinition &biome, const QString &note = {}) const;
    Link featureLink(const QcFeatureDefinition &feature, const QString &note = {}) const;
    Link oreLink(const QcOreDefinition &ore, const QString &note = {}) const;
    Link tableLink(const QString &table) const;

private:
    const QcContentRegistry &m_content;
    const QcHandbookText &m_text;
    QMap<QString, QStringList> m_tagMembers;
};
