#pragma once

#include "QcHandbookEntry.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// The handbook of one content registry in one language (built by QcHandbookBuilder): categories
// with their entries, grouped by pack, and a search over titles in this language and in English.
class QcHandbook
{
public:
    struct Pack
    {
        QString id;
        QString name;
        QString version;
    };

    struct Category
    {
        QString id;
        QString title;
        // Indices into entries(), ordered by pack (base game first) and title.
        QList<qsizetype> entries;
    };

    // Everything a handbook consists of; see the accessors of the same names.
    struct Contents
    {
        QString language;
        QList<Pack> packs;
        QList<Category> categories;
        QList<QcHandbookEntry> entries;
        QStringList problems;
        QMap<QString, QString> labels;
    };

    static constexpr int defaultSearchLimit = 50;

    QcHandbook() = default;
    explicit QcHandbook(Contents contents);

    const QString &language() const { return m_language; }
    // Packs in load order; entries of names without a loaded pack come last under their namespace.
    const QList<Pack> &packs() const { return m_packs; }
    const QList<Category> &categories() const { return m_categories; }
    const QList<QcHandbookEntry> &entries() const { return m_entries; }
    const QcHandbookEntry *entry(const QString &id) const;
    const Category *category(const QString &id) const;
    // Display name of a pack id (its namespace when no such pack was loaded).
    QString packName(const QString &id) const;
    // Inconsistencies found while building (links to unknown entries, unknown rule placeholders).
    const QStringList &problems() const { return m_problems; }
    // Translated texts of the handbook itself for exports ("title", "intro", "contents", "index",
    // "packs", "fact.id"); the key itself when missing.
    QString label(const QString &key) const { return m_labels.value(key, key); }

    // Entries whose title (in this language or English), name or text contains every word of
    // `text`, best matches first: title equals it, title starts with it, a title word starts with it, title or name
    // contains it, then the text. Case and accents are ignored.
    QList<qsizetype> search(const QString &text, int limit = defaultSearchLimit) const;
    // Lower case without accents, as compared by search().
    static QString searchKey(const QString &text);

private:
    // How well entry `index` matches the search words (lower is better, see search()); none when a
    // word is missing.
    std::optional<int> matchRank(qsizetype index, const QString &query, const QStringList &words) const;

    QString m_language;
    QList<Pack> m_packs;
    QList<Category> m_categories;
    QList<QcHandbookEntry> m_entries;
    QStringList m_problems;
    QMap<QString, QString> m_labels;
    QHash<QString, qsizetype> m_index;
    // Per entry: search keys of its titles and of its text.
    QList<QString> m_titleKeys;
    QList<QString> m_textKeys;
    QHash<QString, qsizetype> m_categoryOrder;
};
