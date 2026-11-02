// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcHandbookModel.h"

#include "common/handbook/QcHandbookJson.h"

using namespace Qt::StringLiterals;

namespace {

using Kind = QcHandbookEntry::Recipe::Kind;
static_assert(static_cast<int>(QcHandbookModel::RecipeKind::Crafting) == static_cast<int>(Kind::Crafting));
static_assert(static_cast<int>(QcHandbookModel::RecipeKind::Machine) == static_cast<int>(Kind::Machine));

// The page `page` of `entry` (QcHandbookJson::entry) with each recipe's "kind" as a RecipeKind
// instead of its JSON name; the page's sections and recipes are those of the entry, in order.
[[nodiscard]] QVariantMap withRecipeKinds(QVariantMap page, const QcHandbookEntry &entry)
{
    QVariantList sections = page.value(u"sections"_s).toList();
    for (qsizetype index = 0; index < sections.size() && index < entry.sections.size(); ++index) {
        QVariantMap section = sections[index].toMap();
        QVariantList recipes = section.value(u"recipes"_s).toList();
        const QList<QcHandbookEntry::Recipe> &kinds = entry.sections[index].recipes;
        for (qsizetype recipe = 0; recipe < recipes.size() && recipe < kinds.size(); ++recipe) {
            QVariantMap map = recipes[recipe].toMap();
            map.insert(u"kind"_s, static_cast<int>(kinds[recipe].kind));
            recipes[recipe] = map;
        }
        section.insert(u"recipes"_s, recipes);
        sections[index] = section;
    }
    page.insert(u"sections"_s, sections);
    return page;
}

} // namespace

QcHandbookModel::QcHandbookModel(QObject *parent)
    : QObject(parent)
{
}

void QcHandbookModel::setHandbook(std::shared_ptr<const QcHandbook> handbook)
{
    m_handbook = std::move(handbook);
    m_categories = m_handbook ? QcHandbookJson::categories(*m_handbook).toVariantList() : QVariantList();
    emit handbookChanged();
}

QVariantMap QcHandbookModel::summary(const QcHandbookEntry &entry) const
{
    QVariantMap map = QcHandbookJson::summary(entry).toVariantMap();
    map.insert(u"packName"_s, m_handbook->packName(entry.pack));
    const QcHandbook::Category *category = m_handbook->category(entry.category);
    map.insert(u"categoryTitle"_s, category ? category->title : entry.category);
    return map;
}

QVariantList QcHandbookModel::entries(const QString &category) const
{
    QVariantList list;
    const QcHandbook::Category *found = m_handbook ? m_handbook->category(category) : nullptr;
    if (!found) {
        return list;
    }

    list.reserve(found->entries.size() + 2);
    // Pack headings only when more than the base game is loaded.
    const bool headings = m_handbook->packs().size() > 1;
    QString pack;
    for (const qsizetype index : found->entries) {
        const QcHandbookEntry &entry = m_handbook->entries()[index];
        if (headings && (list.isEmpty() || entry.pack != pack)) {
            pack = entry.pack;
            list.append(QVariantMap{{u"header"_s, m_handbook->packName(pack)}});
        }
        list.append(summary(entry));
    }
    return list;
}

QVariantList QcHandbookModel::search(const QString &text) const
{
    QVariantList list;
    if (!m_handbook) {
        return list;
    }

    const QList<qsizetype> found = m_handbook->search(text);
    for (const qsizetype index : found) {
        list.append(summary(m_handbook->entries()[index]));
    }
    return list;
}

QVariantMap QcHandbookModel::page(const QString &id) const
{
    const QcHandbookEntry *entry = m_handbook ? m_handbook->entry(id) : nullptr;
    return entry ? withRecipeKinds(QcHandbookJson::entry(*m_handbook, *entry).toVariantMap(), *entry) : QVariantMap();
}

QString QcHandbookModel::resolve(const QString &target) const
{
    if (!m_handbook || target.isEmpty()) {
        return {};
    }
    if (m_handbook->entry(target)) {
        return target;
    }

    // Unqualified names ("zombie") are looked up in every pack, base game first.
    QStringList names{target};
    if (!target.contains(u':')) {
        names.clear();
        for (const QcHandbook::Pack &pack : m_handbook->packs()) {
            names.append(pack.id + u':' + target);
        }
    }

    for (const QString &name : std::as_const(names)) {
        for (const QString &prefix : {u"block:"_s, u"item:"_s, u"mob:"_s, u"biome:"_s, u"chapter:"_s}) {
            if (m_handbook->entry(prefix + name)) {
                return prefix + name;
            }
        }
    }
    return {};
}
