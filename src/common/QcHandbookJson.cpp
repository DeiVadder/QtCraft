#include "QcHandbookJson.h"

using namespace Qt::StringLiterals;

namespace {

QJsonValue link(const QcHandbookEntry::Link &link)
{
    if (link.isEmpty()) {
        return QJsonValue::Null;
    }
    return QJsonObject{{u"target"_s, link.target}, {u"text"_s, link.text}, {u"icon"_s, link.icon}, {u"note"_s, link.note}};
}

QJsonArray links(const QList<QcHandbookEntry::Link> &list)
{
    QJsonArray array;
    for (const QcHandbookEntry::Link &item : list) {
        array.append(link(item));
    }
    return array;
}

QJsonObject recipe(const QcHandbookEntry::Recipe &recipe)
{
    return {{u"kind"_s, recipe.kind == QcHandbookEntry::Recipe::Kind::Machine ? u"machine"_s : u"crafting"_s},
            {u"name"_s, recipe.name},
            {u"shapeless"_s, recipe.shapeless},
            {u"needsTable"_s, recipe.needsTable},
            {u"width"_s, recipe.width},
            {u"height"_s, recipe.height},
            {u"cells"_s, links(recipe.cells)},
            {u"result"_s, link(recipe.result)},
            {u"count"_s, recipe.resultCount},
            {u"seconds"_s, recipe.seconds},
            {u"machines"_s, links(recipe.machines)},
            {u"note"_s, recipe.note}};
}

} // namespace

namespace QcHandbookJson {

QJsonObject entry(const QcHandbook &handbook, const QcHandbookEntry &entry)
{
    QJsonArray facts;
    for (const QcHandbookEntry::Fact &fact : entry.facts) {
        facts.append(QJsonObject{{u"label"_s, fact.label}, {u"value"_s, fact.value}, {u"links"_s, links(fact.links)}});
    }
    QJsonArray sections;
    for (const QcHandbookEntry::Section &section : entry.sections) {
        QJsonArray recipes;
        for (const QcHandbookEntry::Recipe &item : section.recipes) {
            recipes.append(recipe(item));
        }
        sections.append(QJsonObject{{u"title"_s, section.title},
                                    {u"text"_s, section.text},
                                    {u"links"_s, links(section.links)},
                                    {u"recipes"_s, recipes}});
    }
    const QcHandbook::Category *category = handbook.category(entry.category);
    return {{u"id"_s, entry.id},
            {u"name"_s, entry.name},
            {u"category"_s, entry.category},
            {u"categoryTitle"_s, category ? category->title : entry.category},
            {u"pack"_s, entry.pack},
            {u"packName"_s, handbook.packName(entry.pack)},
            {u"title"_s, entry.title},
            {u"icon"_s, entry.icon},
            {u"text"_s, entry.text},
            {u"facts"_s, facts},
            {u"sections"_s, sections}};
}

QJsonObject summary(const QcHandbookEntry &entry)
{
    return {{u"id"_s, entry.id}, {u"title"_s, entry.title}, {u"icon"_s, entry.icon}, {u"category"_s, entry.category}, {u"pack"_s, entry.pack}};
}

QJsonArray categories(const QcHandbook &handbook)
{
    QJsonArray array;
    for (const QcHandbook::Category &category : handbook.categories()) {
        if (!category.entries.isEmpty()) {
            array.append(QJsonObject{{u"id"_s, category.id}, {u"title"_s, category.title}, {u"count"_s, category.entries.size()}});
        }
    }
    return array;
}

} // namespace QcHandbookJson
