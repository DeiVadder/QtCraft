#include "QcHandbook.h"

#include <algorithm>

using namespace Qt::StringLiterals;

QcHandbook::QcHandbook(QString language, QList<Pack> packs, QList<Category> categories, QList<QcHandbookEntry> entries,
                       QStringList problems, QMap<QString, QString> labels)
    : m_language(std::move(language))
    , m_packs(std::move(packs))
    , m_categories(std::move(categories))
    , m_entries(std::move(entries))
    , m_problems(std::move(problems))
    , m_labels(std::move(labels))
{
    m_index.reserve(m_entries.size());
    m_titleKeys.reserve(m_entries.size());
    m_textKeys.reserve(m_entries.size());
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const QcHandbookEntry &entry = m_entries[i];
        m_index.insert(entry.id, i);
        // The title in this language comes first, so a prefix match on it ranks highest.
        m_titleKeys.append(searchKey(entry.title) + u'\n' + searchKey(entry.englishTitle) + u'\n' + searchKey(entry.name));
        QString text = entry.text;
        for (const QcHandbookEntry::Section &section : entry.sections) {
            text += u'\n' + section.title + u'\n' + section.text;
        }
        m_textKeys.append(searchKey(text));
    }
    for (qsizetype i = 0; i < m_categories.size(); ++i) {
        m_categoryOrder.insert(m_categories[i].id, i);
    }
}

const QcHandbookEntry *QcHandbook::entry(const QString &id) const
{
    const auto it = m_index.constFind(id);
    return it == m_index.constEnd() ? nullptr : &m_entries[*it];
}

const QcHandbook::Category *QcHandbook::category(const QString &id) const
{
    const auto it = m_categoryOrder.constFind(id);
    return it == m_categoryOrder.constEnd() ? nullptr : &m_categories[*it];
}

QString QcHandbook::packName(const QString &id) const
{
    const auto it = std::ranges::find(m_packs, id, &Pack::id);
    return it == m_packs.end() ? id : it->name;
}

QString QcHandbook::searchKey(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString key;
    key.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing) {
            key.append(c);
        }
    }
    return key.toCaseFolded();
}

QList<qsizetype> QcHandbook::search(const QString &text, int limit) const
{
    const QString query = searchKey(text).simplified();
    if (query.isEmpty() || limit <= 0) {
        return {};
    }
    const QStringList words = query.split(u' ', Qt::SkipEmptyParts);
    struct Match
    {
        int rank = 0;
        qsizetype category = 0;
        qsizetype entry = 0;
    };
    QList<Match> matches;
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        const QString &titles = m_titleKeys[i];
        const QString &body = m_textKeys[i];
        bool inTitles = true;
        bool found = true;
        for (const QString &word : words) {
            if (!titles.contains(word)) {
                inTitles = false;
                if (!body.contains(word)) {
                    found = false;
                    break;
                }
            }
        }
        if (!found) {
            continue;
        }
        int rank = 3;
        if (inTitles) {
            const QString title = titles.section(u'\n', 0, 0);
            if (title == query) {
                rank = -1;
            } else if (title.startsWith(query)) {
                rank = 0;
            } else if (title.contains(u' ' + words.first()) || titles.section(u'\n', 1, 1).startsWith(query)) {
                rank = 1;
            } else {
                rank = 2;
            }
        }
        matches.append({rank, m_categoryOrder.value(m_entries[i].category, m_categories.size()), i});
    }
    std::ranges::stable_sort(matches, [this](const Match &a, const Match &b) {
        if (a.rank != b.rank) {
            return a.rank < b.rank;
        }
        if (a.category != b.category) {
            return a.category < b.category;
        }
        return m_titleKeys[a.entry] < m_titleKeys[b.entry];
    });
    QList<qsizetype> result;
    for (const Match &match : std::as_const(matches)) {
        if (result.size() >= limit) {
            break;
        }
        result.append(match.entry);
    }
    return result;
}
