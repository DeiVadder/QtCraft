#include "common/handbook/builder/QcHandbookItemPages.h"

#include "common/gameplay/QcMining.h"
#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookRecipes.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbookLinks.h"

using namespace Qt::StringLiterals;
using Fact = QcHandbookEntry::Fact;
using Section = QcHandbookEntry::Section;

namespace {

bool isTool(const QcItemDefinition &item)
{
    return !item.tool.isEmpty() || item.attackDamage > 1.0;
}

// Slot, points and set of armour (decision E-234); its durability counts hits.
[[nodiscard]] QList<Fact> armourFacts(const QcHandbookContext &context, const QcArmourDefinition &armour)
{
    QList<Fact> facts;
    const QcHandbookText &text = context.text;
    facts.append({text.label(u"fact.wornOn"_s), text.label(u"slot."_s + qcEquipmentSlotName(armour.slot)), {}});
    facts.append({text.label(u"fact.armourPoints"_s), u"+"_s + QString::number(armour.points), {context.links.tableLink(u"armour"_s)}});
    const QString setKey = u"armour_set."_s + QString(armour.set).replace(u':', u'.');
    facts.append({text.label(u"fact.armourSet"_s), context.content.translations().translate(setKey, text.language()), {}});
    return facts;
}

// Tool, durability, attack, food and fuel values, and lighting fire (decision E-393).
[[nodiscard]] QList<Fact> useFacts(const QcHandbookContext &context, const QcItemDefinition &item)
{
    QList<Fact> facts;
    const QcHandbookText &text = context.text;
    if (!item.tool.isEmpty()) {
        facts.append({text.label(u"fact.tool"_s), text.label(u"value.toolTier"_s).arg(text.toolName(item.tool)).arg(item.toolLevel),
                      {context.links.tableLink(u"tools"_s)}});
        facts.append({text.label(u"fact.toolSpeed"_s), text.label(u"unit.factor"_s).arg(text.number(item.toolSpeed)), {}});
    }
    if (item.armour) {
        facts += armourFacts(context, *item.armour);
    }
    if (item.durability > 0) {
        facts.append({text.label(u"fact.durability"_s), text.label(item.armour ? u"unit.hits"_s : u"unit.uses"_s).arg(item.durability), {}});
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
    if (item.ignites) {
        facts.append({text.label(u"fact.ignites"_s), {}, {context.links.blockLink(u"qtcraft:fire"_s)}});
    }
    return facts;
}

// The fact `label` linking every block whose `into` field names a block (what a hoe tills, what a
// shovel flattens).
Fact blocksTurnedBy(const QcHandbookContext &context, const QString &label, QString QcBlockDefinition::*into)
{
    Fact fact{context.text.label(label), {}, {}};
    for (const QcBlockDefinition &block : context.content.blocks()) {
        if (!(block.*into).isEmpty()) {
            fact.links.append(context.links.blockLink(block.name));
        }
    }
    return fact;
}

// What the item places, empties to and scoops up, and its tags.
[[nodiscard]] QList<Fact> linkFacts(const QcHandbookContext &context, const QcItemDefinition &item)
{
    QList<Fact> facts;
    const QcHandbookText &text = context.text;
    const QcHandbookLinks &links = context.links;

    if (!item.placesBlock.isEmpty()) {
        facts.append({text.label(u"fact.places"_s), {}, {links.blockLink(item.placesBlock)}});
    }
    if (!item.emptiesTo.isEmpty()) {
        facts.append({text.label(u"fact.emptiesTo"_s), {}, {links.itemLink(item.emptiesTo)}});
    }
    if (item.tills) {
        facts.append(blocksTurnedBy(context, u"fact.tills"_s, &QcBlockDefinition::tillsInto));
    }
    if (item.flattens) {
        facts.append(blocksTurnedBy(context, u"fact.flattens"_s, &QcBlockDefinition::flattensInto));
    }
    if (item.carves) {
        facts.append(blocksTurnedBy(context, u"fact.carves"_s, &QcBlockDefinition::carvesInto));
    }
    if (item.strips) {
        facts.append(blocksTurnedBy(context, u"fact.strips"_s, &QcBlockDefinition::strippedInto));
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
    if (item.bottles) {
        Fact fills{text.label(u"fact.fills"_s), {}, {}};
        for (const QcBlockDefinition &block : context.content.blocks()) {
            if (block.liquid && !block.flow.bottle.isEmpty()) {
                fills.links.append(links.blockLink(block.name, links.itemTitle(block.flow.bottle, text.language())));
            }
        }
        facts.append(std::move(fills));
    }

    if (!item.tags.isEmpty()) {
        Fact tags{text.label(u"fact.tags"_s), {}, {}};
        for (const QString &tag : item.tags) {
            tags.links.append(links.tagLink(tag));
        }
        facts.append(std::move(tags));
    }
    return facts;
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

    harvests.links = QcHandbookLinks::sortedLinks(std::move(harvests.links));
    return harvests;
}

QcHandbookEntry itemPage(const QcHandbookContext &context, const QcItemDefinition &item)
{
    QcHandbookText &text = context.text;
    QcHandbookEntry entry;
    entry.id = u"item:"_s + item.name;
    entry.name = item.name;
    entry.category = isTool(item) || item.armour ? u"tools"_s : (item.food > 0 ? u"food"_s : u"items"_s);
    entry.pack = QcHandbookLinks::packOf(item.name);
    entry.title = context.content.translations().translate(item.translationKey, text.language());
    entry.icon = item.name;
    entry.text = text.description(item.descriptionKey, item.name);

    entry.facts.append({text.label(u"fact.id"_s), item.name, {}});
    entry.facts.append({text.label(u"fact.maxStack"_s), QString::number(item.maxStack), {}});
    entry.facts += useFacts(context, item);
    entry.facts += linkFacts(context, item);

    entry.sections += context.recipes.itemSections(item.name);
    if (!item.tool.isEmpty()) {
        Section harvests = harvestSection(context, item);
        if (!harvests.links.isEmpty()) {
            entry.sections.append(std::move(harvests));
        }
    }

    entry.englishTitle = context.content.translations().translate(item.translationKey, QcHandbookText::english());
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
