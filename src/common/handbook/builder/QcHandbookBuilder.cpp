// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/handbook/builder/QcHandbookBuilder.h"

#include "common/handbook/builder/QcHandbookBlockPages.h"
#include "common/handbook/builder/QcHandbookBlockTables.h"
#include "common/handbook/builder/QcHandbookChapterPages.h"
#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookCreaturePages.h"
#include "common/handbook/builder/QcHandbookIndex.h"
#include "common/handbook/builder/QcHandbookItemPages.h"
#include "common/handbook/builder/QcHandbookOverviewPages.h"
#include "common/handbook/builder/QcHandbookRecipes.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/builder/QcHandbookWorldPages.h"
#include "common/handbook/QcHandbookLinks.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;
using Section = QcHandbookEntry::Section;

namespace {

void checkLink(const QcHandbookEntry &entry, const Link &link, const QSet<QString> &ids, QcHandbookText &text)
{
    if (link.isEmpty()) {
        return;
    }

    if (link.target.isEmpty()) {
        text.addProblem(u"%1: '%2' has no entry"_s.arg(entry.id, link.text));
    } else if (!ids.contains(link.target)) {
        text.addProblem(u"%1: link to unknown entry %2"_s.arg(entry.id, link.target));
    }
}

void checkLinks(const QcHandbookEntry &entry, const QList<Link> &links, const QSet<QString> &ids, QcHandbookText &text)
{
    for (const Link &link : links) {
        checkLink(entry, link, ids, text);
    }
}

// Reports duplicate entries, links without an entry and links to unknown entries.
void checkLinks(const QList<QcHandbookEntry> &entries, QcHandbookText &text)
{
    QSet<QString> ids;
    for (const QcHandbookEntry &entry : entries) {
        if (ids.contains(entry.id)) {
            text.addProblem(u"duplicate entry %1"_s.arg(entry.id));
        }
        ids.insert(entry.id);
    }

    for (const QcHandbookEntry &entry : entries) {
        for (const QcHandbookEntry::Fact &fact : entry.facts) {
            checkLinks(entry, fact.links, ids, text);
        }
        for (const Section &section : entry.sections) {
            checkLinks(entry, section.links, ids, text);
            for (const QcHandbookEntry::Recipe &recipe : section.recipes) {
                checkLinks(entry, recipe.cells, ids, text);
                checkLink(entry, recipe.result, ids, text);
                checkLinks(entry, recipe.machines, ids, text);
            }
        }
    }
}

// Loaded packs in load order, then the namespaces of entries without a loaded pack, sorted.
QList<QcHandbook::Pack> handbookPacks(const QcContentRegistry &content, const QList<QcHandbookEntry> &entries)
{
    QList<QcHandbook::Pack> packs;
    QSet<QString> known;
    for (const QcPackReference &pack : content.packs()) {
        if (!known.contains(pack.id)) {
            known.insert(pack.id);
            packs.append({pack.id, pack.name, pack.version});
        }
    }

    QStringList otherNamespaces;
    for (const QcHandbookEntry &entry : entries) {
        if (!known.contains(entry.pack) && !otherNamespaces.contains(entry.pack)) {
            otherNamespaces.append(entry.pack);
        }
    }

    std::ranges::sort(otherNamespaces);
    for (const QString &name : std::as_const(otherNamespaces)) {
        packs.append({name, name, {}});
    }
    return packs;
}

// Category entries by pack, then chapter order and title; tables keep their order within a pack.
void sortCategory(QcHandbook::Category &category, const QList<QcHandbookEntry> &entries, const QHash<QString, qsizetype> &packOrder,
                  const QHash<QString, int> &chapterOrder, const QList<QString> &sortKeys)
{
    const bool fixedOrder = category.id == u"tables"_s;
    std::ranges::stable_sort(category.entries, [&](qsizetype a, qsizetype b) {
        const QcHandbookEntry &first = entries[a];
        const QcHandbookEntry &second = entries[b];
        const qsizetype packA = packOrder.value(first.pack);
        const qsizetype packB = packOrder.value(second.pack);
        if (packA != packB) {
            return packA < packB;
        }

        if (fixedOrder) {
            return false;
        }

        const int orderA = chapterOrder.value(first.id);
        const int orderB = chapterOrder.value(second.id);
        if (orderA != orderB) {
            return orderA < orderB;
        }
        return sortKeys[a] != sortKeys[b] ? sortKeys[a] < sortKeys[b] : first.id < second.id;
    });
}

QList<QcHandbook::Category> handbookCategories(const QcContentRegistry &content, const QcHandbookText &text,
                                               const QList<QcHandbookEntry> &entries, const QList<QcHandbook::Pack> &packs)
{
    static constexpr std::array<const char16_t *, 12> categoryIds{u"playing", u"blocks", u"parts", u"liquids", u"items", u"tools",
                                                                  u"food", u"creatures", u"biomes", u"worldgen", u"tags", u"tables"};

    QList<QcHandbook::Category> categories;
    QHash<QString, qsizetype> categoryIndex;
    for (const char16_t *id : categoryIds) {
        const QString name = QString::fromUtf16(id);
        categoryIndex.insert(name, categories.size());
        categories.append({name, text.label(u"category."_s + name), {}});
    }

    QHash<QString, qsizetype> packOrder;
    for (qsizetype i = 0; i < packs.size(); ++i) {
        packOrder.insert(packs[i].id, i);
    }

    QHash<QString, int> chapterOrder;
    for (const QcHandbookChapter &chapter : content.handbookChapters()) {
        chapterOrder.insert(u"chapter:"_s + chapter.name, chapter.order);
    }

    QList<QString> sortKeys;
    sortKeys.reserve(entries.size());
    for (qsizetype i = 0; i < entries.size(); ++i) {
        categories[categoryIndex.value(entries[i].category)].entries.append(i);
        sortKeys.append(QcHandbook::searchKey(entries[i].title));
    }

    for (QcHandbook::Category &category : categories) {
        sortCategory(category, entries, packOrder, chapterOrder, sortKeys);
    }
    return categories;
}

QMap<QString, QString> handbookLabels(QcHandbookText &text)
{
    QMap<QString, QString> labels;
    for (const QString &key : {u"title"_s, u"intro"_s, u"contents"_s, u"index"_s, u"packs"_s, u"entries"_s, u"fact.id"_s}) {
        labels.insert(key, key == u"intro"_s ? text.substituteRules(text.label(key), u"handbook intro"_s) : text.label(key));
    }
    return labels;
}

QList<QcHandbookEntry> buildEntries(const QcContentRegistry &content, QcHandbookText &text)
{
    const QcHandbookLinks links(content, text);
    const QcHandbookIndex index(content, text, links);
    const QcHandbookRecipes recipes(content, text, links, index);
    const QcHandbookContext context{content, text, links, index, recipes};

    QList<QcHandbookEntry> entries = QcHandbookChapterPages::build(context);
    entries += QcHandbookBlockPages::build(context);
    entries += QcHandbookItemPages::build(context);
    entries += QcHandbookCreaturePages::build(context);
    entries += QcHandbookWorldPages::build(context);
    entries += QcHandbookOverviewPages::build(context);
    entries += QcHandbookBlockTables::build(context);
    return entries;
}

} // namespace

namespace QcHandbookBuilder {

QcHandbook build(const QcContentRegistry &content, const QString &language, const QMap<QString, double> &rules,
                 const QMap<QString, QStringList> &keys)
{
    QcHandbookText text(content, language, rules, keys);
    QcHandbook::Contents contents;
    contents.language = language;

    contents.entries = buildEntries(content, text);
    checkLinks(contents.entries, text);

    contents.packs = handbookPacks(content, contents.entries);
    contents.categories = handbookCategories(content, text, contents.entries, contents.packs);
    contents.labels = handbookLabels(text);
    contents.problems = text.takeProblems();

    return QcHandbook(std::move(contents));
}

} // namespace QcHandbookBuilder
