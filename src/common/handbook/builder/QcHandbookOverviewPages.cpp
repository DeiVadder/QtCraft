#include "common/handbook/builder/QcHandbookOverviewPages.h"

#include "common/gameplay/QcArmour.h"
#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookRecipes.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbook.h"
#include "common/handbook/QcHandbookLinks.h"
#include "common/handbook/QcLinkList.h"

#include <algorithm>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;
using Recipe = QcHandbookEntry::Recipe;
using Section = QcHandbookEntry::Section;

namespace {

using QcHandbookOverviewPages::orderedLinks;
using QcHandbookOverviewPages::tablePage;

QcHandbookEntry tagPage(const QcHandbookContext &context, const QString &tag, const QStringList &members)
{
    const QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;
    QcHandbookEntry entry;
    entry.id = u"tag:"_s + tag;
    entry.name = tag;
    entry.category = u"tags"_s;
    entry.pack = QcHandbookLinks::packOf(tag);
    entry.title = links.tagTitle(tag, text.language());
    entry.icon = links.itemIcon(members.first());
    entry.text = text.label(u"tag.text"_s);
    entry.facts.append({text.label(u"fact.id"_s), u"#"_s + tag, {}});

    Section memberSection{text.label(u"section.members"_s), {}, {}, {}};
    for (const QString &member : members) {
        memberSection.links.append(links.itemLink(member));
    }
    memberSection.links = QcHandbookLinks::sortedLinks(std::move(memberSection.links));
    entry.sections.append(std::move(memberSection));

    QcLinkList usedBy;
    const QString ingredient = u"#"_s + tag;
    for (const QcRecipeDefinition &recipe : context.content.recipes()) {
        if (recipe.ingredients.contains(ingredient)
            || std::ranges::any_of(recipe.keys, [&ingredient](const auto &key) { return key.second == ingredient; })) {
            usedBy.addUnique(links.itemLink(recipe.resultItem));
        }
    }
    Section usedIn{text.label(u"section.usedIn"_s), {}, usedBy.sorted(), {}};
    if (!usedIn.links.isEmpty()) {
        entry.sections.append(std::move(usedIn));
    }

    entry.englishTitle = links.tagTitle(tag, QcHandbookText::english());
    return entry;
}

// Fuels by burning time (longest first) and the machines burning them.
QcHandbookEntry fuelTable(const QcHandbookContext &context)
{
    const QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;
    QcHandbookEntry fuels = tablePage(context, u"fuels"_s, u"qtcraft:coal"_s);

    QList<std::pair<double, Link>> burning;
    for (const QcItemDefinition &item : context.content.items()) {
        if (item.fuelSeconds > 0.0) {
            burning.append({item.fuelSeconds, links.itemLink(item.name, text.seconds(item.fuelSeconds))});
        }
    }
    fuels.sections.append({text.label(u"section.fuels"_s), {}, orderedLinks(std::move(burning), true), {}});

    Section machines{text.label(u"section.machines"_s), {}, {}, {}};
    for (const QcBlockDefinition &block : context.content.blocks()) {
        if (block.machine.isMachine()) {
            machines.links.append(links.blockLink(block.name));
        }
    }
    machines.links = QcHandbookLinks::sortedLinks(std::move(machines.links));
    fuels.sections.append(std::move(machines));
    return fuels;
}

// One tool kind: its tiers (weakest first) and, per level, the blocks that need it.
Section toolSection(const QcHandbookContext &context, const QString &category)
{
    const QcHandbookText &text = context.text;
    QList<std::pair<int, Link>> tiers;
    for (const QcItemDefinition &item : context.content.items()) {
        if (item.tool == category) {
            tiers.append({item.toolLevel, context.links.itemLink(item.name, text.label(u"value.tier"_s)
                                                                               .arg(item.toolLevel)
                                                                               .arg(text.number(item.toolSpeed))
                                                                               .arg(item.durability))});
        }
    }

    Section section{text.toolName(category), {}, orderedLinks(std::move(tiers)), {}};
    QMap<int, QStringList> needs;
    for (const QcBlockDefinition &block : context.content.blocks()) {
        if (block.tool == category && block.toolLevel > 0 && block.partOf.isEmpty()) {
            needs[block.toolLevel].append(context.content.translations().translate(block.translationKey, text.language()));
        }
    }

    QStringList lines;
    for (auto it = needs.begin(); it != needs.end(); ++it) {
        std::ranges::sort(*it, [](const QString &a, const QString &b) { return QcHandbook::searchKey(a) < QcHandbook::searchKey(b); });
        lines.append(u"- "_s + text.label(u"value.levelNeeds"_s).arg(it.key()).arg(it->join(u", "_s)));
    }
    section.text = lines.join(u'\n');
    return section;
}

QcHandbookEntry toolTable(const QcHandbookContext &context)
{
    QcHandbookEntry tools = tablePage(context, u"tools"_s, u"qtcraft:iron_pickaxe"_s);

    QStringList categories;
    for (const QcItemDefinition &item : context.content.items()) {
        if (!item.tool.isEmpty() && !categories.contains(item.tool)) {
            categories.append(item.tool);
        }
    }

    for (const QString &category : std::as_const(categories)) {
        tools.sections.append(toolSection(context, category));
    }
    return tools;
}

// Every armour set with its pieces (head to feet: points and durability) and the rule.
QcHandbookEntry armourTable(const QcHandbookContext &context)
{
    const QcHandbookText &text = context.text;
    const std::optional<QcItemId> icon = QcArmour::pointsIcon(context.content);
    QcHandbookEntry armour = tablePage(context, u"armour"_s, icon ? context.content.item(*icon).name : QString());

    QMap<QString, QList<std::pair<int, Link>>> sets;
    QStringList order;
    for (const QcItemDefinition &item : context.content.items()) {
        if (!item.armour) {
            continue;
        }
        if (!sets.contains(item.armour->set)) {
            order.append(item.armour->set);
        }
        const QString detail = text.label(u"value.armourPiece"_s)
                                   .arg(text.label(u"slot."_s + qcEquipmentSlotName(item.armour->slot)))
                                   .arg(item.armour->points)
                                   .arg(item.durability);
        sets[item.armour->set].append({int(item.armour->slot), context.links.itemLink(item.name, detail)});
    }

    for (const QString &set : std::as_const(order)) {
        const QString title = context.content.translations().translate(u"armour_set."_s + QString(set).replace(u':', u'.'), text.language());
        armour.sections.append({title, {}, orderedLinks(sets.take(set)), {}});
    }
    return armour;
}

// All machine recipes by result.
QcHandbookEntry smeltingTable(const QcHandbookContext &context)
{
    QcHandbookEntry smelting = tablePage(context, u"smelting"_s, u"qtcraft:furnace"_s);
    Section all{context.text.label(u"section.machineRecipes"_s), {}, {}, {}};
    for (const QcSmeltingRecipe &recipe : context.content.smeltingRecipes()) {
        all.recipes.append(context.recipes.machineRecipe(recipe));
    }

    std::ranges::stable_sort(all.recipes, [](const Recipe &a, const Recipe &b) {
        const QString keyA = QcHandbook::searchKey(a.result.text);
        const QString keyB = QcHandbook::searchKey(b.result.text);
        return keyA != keyB ? keyA < keyB : a.name < b.name;
    });

    all.links.append(context.links.tableLink(u"fuels"_s));
    smelting.sections.append(std::move(all));
    return smelting;
}

} // namespace

namespace QcHandbookOverviewPages {

QcHandbookEntry tablePage(const QcHandbookContext &context, const QString &name, const QString &iconItem)
{
    QcHandbookEntry entry;
    entry.id = u"table:"_s + name;
    entry.category = u"tables"_s;
    entry.pack = context.links.basePack();
    entry.title = context.text.label(u"table."_s + name);
    entry.text = context.text.label(u"table."_s + name + u"Text"_s);
    entry.icon = context.links.itemIcon(iconItem);
    entry.englishTitle = context.content.translations().translate(u"handbook.table."_s + name, QcHandbookText::english());
    return entry;
}

QList<QcHandbookEntry> build(const QcHandbookContext &context)
{
    QList<QcHandbookEntry> entries;
    const QMap<QString, QStringList> &tags = context.links.tagMembers();
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        entries.append(tagPage(context, it.key(), *it));
    }

    entries.append(fuelTable(context));
    entries.append(toolTable(context));
    entries.append(armourTable(context));
    entries.append(smeltingTable(context));
    return entries;
}

} // namespace QcHandbookOverviewPages
