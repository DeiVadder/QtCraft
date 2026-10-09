#include "QcHandbookItemPages.h"

#include "QcHandbookContext.h"
#include "QcHandbookLinks.h"
#include "QcHandbookRecipes.h"
#include "QcHandbookText.h"
#include "QcMining.h"

using namespace Qt::StringLiterals;
using Fact = QcHandbookEntry::Fact;
using Section = QcHandbookEntry::Section;

namespace {

bool isTool(const QcItemDefinition &item)
{
    return !item.tool.isEmpty() || item.attackDamage > 1.0;
}

// Tool, durability, attack, food and fuel values.
void addUseFacts(const QcHandbookContext &context, const QcItemDefinition &item, QList<Fact> &facts)
{
    const QcHandbookText &text = context.text;
    if (!item.tool.isEmpty()) {
        facts.append({text.label(u"fact.tool"_s), text.label(u"value.toolTier"_s).arg(text.toolName(item.tool)).arg(item.toolLevel),
                      {context.links.tableLink(u"tools"_s)}});
        facts.append({text.label(u"fact.toolSpeed"_s), text.label(u"unit.factor"_s).arg(text.number(item.toolSpeed)), {}});
    }
    if (item.durability > 0) {
        facts.append({text.label(u"fact.durability"_s), text.label(u"unit.uses"_s).arg(item.durability), {}});
    }
    if (isTool(item)) {
        facts.append({text.label(u"fact.attackDamage"_s), text.healthPoints(item.attackDamage), {}});
    }
    if (item.food > 0) {
        facts.append({text.label(u"fact.food"_s), text.foodPoints(item.food), {}});
    }
    if (item.fuelSeconds > 0.0) {
        facts.append({text.label(u"fact.fuel"_s), text.seconds(item.fuelSeconds), {context.links.tableLink(u"fuels"_s)}});
    }
}

// What the item places, empties to and scoops up, and its tags.
void addLinkFacts(const QcHandbookContext &context, const QcItemDefinition &item, QList<Fact> &facts)
{
    const QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;

    if (!item.placesBlock.isEmpty()) {
        facts.append({text.label(u"fact.places"_s), {}, {links.blockLink(item.placesBlock)}});
    }
    if (!item.emptiesTo.isEmpty()) {
        facts.append({text.label(u"fact.emptiesTo"_s), {}, {links.itemLink(item.emptiesTo)}});
    }

    if (item.scoops) {
        Fact scoops{text.label(u"fact.scoops"_s), {}, {}};
        for (const QcBlockDefinition &block : context.content.blocks()) {
            if (block.liquid && !block.flow.bucket.isEmpty()) {
                scoops.links.append(links.blockLink(block.name, links.itemTitle(block.flow.bucket, text.language())));
            }
        }
        facts.append(std::move(scoops));
    }

    if (!item.tags.isEmpty()) {
        Fact tags{text.label(u"fact.tags"_s), {}, {}};
        for (const QString &tag : item.tags) {
            tags.links.append(links.tagLink(tag));
        }
        facts.append(std::move(tags));
    }
}

// Blocks that need a tool of the item's kind and level to drop anything.
Section harvestSection(const QcHandbookContext &context, const QcItemDefinition &item)
{
    const QcHandbookText &text = context.text;
    Section harvests{text.label(u"section.harvests"_s), {}, {}, {}};
    for (const QcBlockDefinition &block : context.content.blocks()) {
        if (block.renderLayer != QcRenderLayer::Invisible && block.tool == item.tool && block.toolLevel > 0
            && QcMining::canHarvest(block, &item)) {
            harvests.links.append(context.links.blockLink(block.name, text.label(u"unit.level"_s).arg(block.toolLevel)));
        }
    }

    QcHandbookLinks::sortLinks(harvests.links);
    return harvests;
}

QcHandbookEntry itemPage(const QcHandbookContext &context, const QcItemDefinition &item)
{
    QcHandbookText &text = context.text;
    QcHandbookEntry entry;
    entry.id = u"item:"_s + item.name;
    entry.name = item.name;
    entry.category = isTool(item) ? u"tools"_s : (item.food > 0 ? u"food"_s : u"items"_s);
    entry.pack = QcHandbookLinks::packOf(item.name);
    entry.title = context.content.translate(item.translationKey, text.language());
    entry.icon = item.name;
    entry.text = text.description(item.descriptionKey, item.name);

    entry.facts.append({text.label(u"fact.id"_s), item.name, {}});
    entry.facts.append({text.label(u"fact.maxStack"_s), QString::number(item.maxStack), {}});
    addUseFacts(context, item, entry.facts);
    addLinkFacts(context, item, entry.facts);

    context.recipes.addItemSections(entry, item.name);
    if (!item.tool.isEmpty()) {
        Section harvests = harvestSection(context, item);
        if (!harvests.links.isEmpty()) {
            entry.sections.append(std::move(harvests));
        }
    }

    entry.englishTitle = context.content.translate(item.translationKey, QcHandbookText::english());
    return entry;
}

} // namespace

namespace QcHandbookItemPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context)
{
    QList<QcHandbookEntry> entries;
    for (const QcItemDefinition &item : context.content.items()) {
        if (!context.links.hasBlockEntry(item.name)) {
            entries.append(itemPage(context, item));
        }
    }
    return entries;
}

} // namespace QcHandbookItemPages
