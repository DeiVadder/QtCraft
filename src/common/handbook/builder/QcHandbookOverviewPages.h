#pragma once

#include "common/handbook/QcHandbook.h"
#include "common/handbook/QcHandbookEntry.h"

#include <QList>

#include <algorithm>
#include <utility>

struct QcHandbookContext;

// Handbook pages that gather content from all over: the item tags (category "tags", by tag name)
// followed by the tables of fuels, tools, armour and machine recipes (category "tables", in that
// order; the block tables of QcHandbookBlockTables follow them).
namespace QcHandbookOverviewPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context);

// An empty table page "table:<name>" of the base pack: title and text from the labels
// "table.<name>" and "table.<name>Text", the icon of `iconItem`.
QcHandbookEntry tablePage(const QcHandbookContext &context, const QString &name, const QString &iconItem);

// Links ordered by their key (ascending unless `descending`), then by text as the search compares it.
template<typename Key>
QList<QcHandbookEntry::Link> orderedLinks(QList<std::pair<Key, QcHandbookEntry::Link>> keyed, bool descending = false)
{
    std::ranges::stable_sort(keyed, [descending](const auto &a, const auto &b) {
        if (a.first != b.first) {
            return descending ? a.first > b.first : a.first < b.first;
        }
        return QcHandbook::searchKey(a.second.text) < QcHandbook::searchKey(b.second.text);
    });

    QList<QcHandbookEntry::Link> links;
    for (auto &[key, link] : keyed) {
        links.append(std::move(link));
    }
    return links;
}

} // namespace QcHandbookOverviewPages
