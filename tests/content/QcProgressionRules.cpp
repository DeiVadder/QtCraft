// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "content/QcProgressionRules.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcMining.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

namespace Node = QcProgressionNode;

// A tool of the kind `tool` at least as good as `level`.
QString toolNode(const QString &tool, int level)
{
    return u"%1 of level %2"_s.arg(tool).arg(level);
}

const QString craftingGrid = u"crafting grid of 3 x 3"_s;
const QString stonecutter = u"stonecutter"_s;
const QString fuel = u"fuel"_s;

// A machine that takes recipes of `category` and `kind`.
QString machineNode(const QString &category, const QString &kind)
{
    return kind.isEmpty() ? u"machine %1"_s.arg(category) : u"machine %1 for %2"_s.arg(category, kind);
}

// An ingredient of a recipe: an item, or "#tag" for any item of the tag.
QString ingredientNode(const QString &ingredient)
{
    return ingredient.startsWith(u'#') ? u"tag "_s + ingredient.mid(1) : Node::item(ingredient);
}

// Collects the rules of a content.
class RuleBuilder
{
public:
    explicit RuleBuilder(const QcContentRegistry &content)
        : m_content(content)
    {
    }

    [[nodiscard]] QList<QcProgressionRule> build();

private:
    void add(QcProgressionRule rule) { m_rules.append(std::move(rule)); }
    void addRecipes();
    void addMachines();
    void addBlock(const QcBlockDefinition &block);
    void addDrops(const QcBlockDefinition &block);
    void addLiquid(const QcBlockDefinition &block);
    void addItem(const QcItemDefinition &item);
    void addPlacing(const QcItemDefinition &item);
    void addCreature(const QcMobDefinition &mob);
    void addChests();
    void addTrades();
    void addPortal(const QcPortalDefinition &portal);
    // The node of having `count` of `node`, with its rule.
    [[nodiscard]] QString gather(const QString &node, int count);
    // The way through `portal` from `here` to `there`.
    void addPortalWay(const QcPortalDefinition &portal, const QString &here, const QString &there);
    [[nodiscard]] QStringList dimensionsWithWater() const;

    const QcContentRegistry &m_content;
    QList<QcProgressionRule> m_rules;
};

void RuleBuilder::addRecipes()
{
    for (const QcRecipeDefinition &recipe : m_content.recipes()) {
        QSet<QString> needs;
        for (const auto &[key, item] : recipe.keys) {
            needs.insert(ingredientNode(item));
        }
        for (const QString &ingredient : recipe.ingredients) {
            needs.insert(ingredientNode(ingredient));
        }
        if (QcCrafting::gridSizeNeeded(recipe) > 2) {
            needs.insert(craftingGrid);
        }
        add({u"recipe %1"_s.arg(recipe.name), needs.values(), {Node::item(recipe.resultItem)}});
    }
}

void RuleBuilder::addMachines()
{
    for (const QcSmeltingRecipe &recipe : m_content.smeltingRecipes()) {
        const QString machine = machineNode(recipe.category, recipe.kind);
        add({u"smelting %1"_s.arg(recipe.name), {machine, fuel, Node::item(recipe.inputItem)}, {Node::item(recipe.resultItem)}});
        for (const QcBlockDefinition &block : m_content.blocks()) {
            if (block.machine.category == recipe.category && block.machine.accepts(recipe.kind)) {
                add({u"standing machine"_s, {Node::block(block.name)}, {machine}});
            }
        }
    }
    for (const QcSmeltingRecipe &recipe : m_content.stonecuttingRecipes()) {
        add({u"stonecutting %1"_s.arg(recipe.name), {stonecutter, Node::item(recipe.inputItem)}, {Node::item(recipe.resultItem)}});
    }
}

void RuleBuilder::addDrops(const QcBlockDefinition &block)
{
    if (!QcMining::isBreakable(block)) {
        return;
    }

    QStringList needs{Node::block(block.name)};
    if (block.toolLevel > 0) {
        needs.append(toolNode(block.tool, block.toolLevel));
    }
    for (const QcItemDrop &drop : block.drops) {
        if (drop.chance > 0.0) {
            add({u"breaking %1"_s.arg(block.name), needs, {Node::item(drop.item)}, 0, Node::block(block.name), drop.mostCount()});
        }
    }
}

void RuleBuilder::addLiquid(const QcBlockDefinition &block)
{
    const QString here = Node::block(block.name);
    for (const QcLiquidReaction &reaction : block.flow.reactions) {
        QStringList results;
        for (const QString &result : {reaction.source, reaction.flowing}) {
            if (!result.isEmpty()) {
                results.append(Node::block(result));
            }
        }
        add({u"%1 meets %2"_s.arg(block.name, reaction.with), {here, Node::block(reaction.with)}, results});
    }
    if (!block.flow.bucket.isEmpty()) {
        add({u"scooping %1"_s.arg(block.name), {here, u"something that scoops"_s}, {Node::item(block.flow.bucket)}});
    }
    if (!block.flow.bottle.isEmpty()) {
        add({u"bottling %1"_s.arg(block.name), {here, u"something that bottles"_s}, {Node::item(block.flow.bottle)}});
    }
}

void RuleBuilder::addBlock(const QcBlockDefinition &block)
{
    const QString here = Node::block(block.name);
    addDrops(block);
    addLiquid(block);
    if (block.opensCrafting) {
        add({u"standing table"_s, {here}, {craftingGrid}});
    }
    if (block.opensStonecutter) {
        add({u"standing stonecutter"_s, {here}, {stonecutter}});
    }
    if (block.fire.sparks > 0) {
        add({u"sparks of %1"_s.arg(block.name), {here}, {Node::lighter()}});
    }
}

QStringList RuleBuilder::dimensionsWithWater() const
{
    QStringList names{QString::fromLatin1(QcDimensionDefinition::overworldName)};
    for (const QcDimensionDefinition &dimension : m_content.worldgen().dimensions()) {
        if (!dimension.waterEvaporates && !names.contains(dimension.name)) {
            names.append(dimension.name);
        }
    }
    return names;
}

void RuleBuilder::addPlacing(const QcItemDefinition &item)
{
    const std::optional<QcBlockId> placed = m_content.blockId(item.placesBlock);
    const QString step = u"placing %1"_s.arg(item.name);
    const QString block = Node::block(item.placesBlock);
    if (!placed || !m_content.block(*placed).isWater()) {
        add({step, {Node::item(item.name)}, {block}, 0, Node::item(item.name), 1});
        return;
    }

    // Stricter than the game today: no server code reads waterEvaporates yet, a bucket still pours
    // water in the nether. The rule follows the data, so no way may lean on it.
    const QStringList dimensions = dimensionsWithWater();
    for (const QString &dimension : dimensions) {
        add({step, {Node::item(item.name), Node::dimension(dimension)}, {block}, 0, Node::item(item.name), 1});
    }
}

void RuleBuilder::addItem(const QcItemDefinition &item)
{
    const QString here = Node::item(item.name);
    if (!item.placesBlock.isEmpty()) {
        addPlacing(item);
    }
    for (int level = 1; level <= item.toolLevel && !item.tool.isEmpty(); ++level) {
        add({u"holding %1"_s.arg(item.name), {here}, {toolNode(item.tool, level)}});
    }
    if (item.fuelSeconds > 0.0) {
        add({u"burning %1"_s.arg(item.name), {here}, {fuel}});
    }
    if (item.food > 0) {
        add({u"eating %1"_s.arg(item.name), {here}, {Node::food()}});
    }
    if (item.ignites) {
        add({u"lighting %1"_s.arg(item.name), {here}, {Node::lighter()}});
    }
    if (item.scoops) {
        add({u"holding %1"_s.arg(item.name), {here}, {u"something that scoops"_s}});
    }
    if (item.bottles) {
        add({u"holding %1"_s.arg(item.name), {here}, {u"something that bottles"_s}});
    }
    for (const QString &tag : item.tags) {
        add({u"%1 counts as %2"_s.arg(item.name, tag), {here}, {u"tag "_s + tag}});
    }
}

void RuleBuilder::addCreature(const QcMobDefinition &mob)
{
    const QString here = Node::creature(mob.name);
    for (const QcItemDrop &drop : mob.drops) {
        if (drop.chance > 0.0) {
            add({u"killing %1"_s.arg(mob.name), {here}, {Node::item(drop.item)}});
        }
    }
    if (!mob.barter) {
        return;
    }

    const QcLootTableDefinition *table = m_content.lootTables().find(mob.barter->table);
    for (const QcLootTableDefinition::Pool &pool : table ? table->pools : QList<QcLootTableDefinition::Pool>{}) {
        for (const QcLootTableDefinition::Entry &entry : pool.entries) {
            if (!entry.item.isEmpty() && entry.weight > 0) {
                add({u"bartering with %1"_s.arg(mob.name), {here, Node::item(mob.barter->currency)}, {Node::item(entry.item)}});
            }
        }
    }
}

void RuleBuilder::addChests()
{
    for (const QcLootTableDefinition &table : m_content.lootTables().all()) {
        for (const QcLootTableDefinition::Pool &pool : table.pools) {
            for (const QcLootTableDefinition::Entry &entry : pool.entries) {
                if (!entry.item.isEmpty() && entry.weight > 0 && pool.maxRolls > 0) {
                    add({u"looting %1"_s.arg(table.name), {Node::chest(table.name)}, {Node::item(entry.item)}, 0, Node::chest(table.name), entry.maxCount * pool.maxRolls});
                }
            }
        }
    }
}

void RuleBuilder::addTrades()
{
    const auto levelNode = [](const QString &profession, int level) { return u"%1 of level %2"_s.arg(profession).arg(level); };
    for (const QcTradeSet &set : m_content.villageCatalog().tradeSets()) {
        for (const QcTradeOffer &offer : set.offers) {
            QStringList needs{Node::villagers()};
            for (const QcTradeStack &cost : offer.costs) {
                needs.append(Node::item(cost.item));
            }
            if (offer.level > 1) {
                // A villager rises by the trades of his level.
                needs.append(levelNode(set.profession, offer.level - 1));
            }
            add({u"trade %1 of %2"_s.arg(offer.id, set.profession), needs, {Node::item(offer.result.item), levelNode(set.profession, offer.level)}});
        }
    }
}

QString RuleBuilder::gather(const QString &node, int count)
{
    const QString enough = u"%1 x %2"_s.arg(count).arg(node);
    add({u"gathering %1"_s.arg(enough), {node}, {enough}, 0, {}, 1, count});
    return enough;
}

void RuleBuilder::addPortalWay(const QcPortalDefinition &portal, const QString &here, const QString &there)
{
    // Corners are not needed; a ring takes an insert in every frame block.
    const int blocks = 2 * (QcPortalDefinition::innerOf(portal.minWidth) + QcPortalDefinition::innerOf(portal.minHeight));
    QStringList needs{Node::dimension(here), gather(Node::item(portal.frame), blocks)};
    needs.append(portal.trigger == QcPortalDefinition::Trigger::Fire ? Node::lighter() : gather(Node::item(portal.triggerItem), blocks));
    add({u"lighting portal %1 in %2"_s.arg(portal.name, here), needs, {Node::portal(portal.name), Node::dimension(there)}});
}

void RuleBuilder::addPortal(const QcPortalDefinition &portal)
{
    addPortalWay(portal, portal.from, portal.to);
    addPortalWay(portal, portal.to, portal.from);
}

QList<QcProgressionRule> RuleBuilder::build()
{
    addRecipes();
    addMachines();
    for (const QcBlockDefinition &block : m_content.blocks()) {
        addBlock(block);
    }
    for (const QcItemDefinition &item : m_content.items()) {
        addItem(item);
    }
    for (const QcMobDefinition &mob : m_content.mobs()) {
        addCreature(mob);
    }
    addChests();
    addTrades();
    for (const QcPortalDefinition &portal : m_content.worldgen().portals()) {
        addPortal(portal);
    }
    return m_rules;
}

QcProgressionResult resultOf(const QcProgressionGoal &goal, const QcProgressionSolver &solver)
{
    const QString node = QcProgressionRules::nodeOf(goal);
    if (!solver.reached(node)) {
        const QcProgressionGap gap = solver.gapTo(node);
        return {goal.name, false, gap.missing, gap.path, {}};
    }

    const qint64 supply = solver.supply(node);
    if (supply < goal.count) {
        return {goal.name, false, {u"%1 of %2 %3"_s.arg(goal.count - supply).arg(goal.count).arg(node)}, {node}, {}};
    }
    return {goal.name, true, {}, {}, solver.wayTo(node)};
}

} // namespace

QString QcProgressionResult::describe() const
{
    if (met) {
        return u"%1: reached by %2"_s.arg(goal, way.join(u", "_s));
    }

    constexpr qsizetype shown = 8;
    const QString more = missing.size() > shown ? u" and %1 more"_s.arg(missing.size() - shown) : QString();
    return u"%1: missing %2%3 (way: %4)"_s.arg(goal, missing.first(std::min(shown, missing.size())).join(u", "_s), more, path.join(u" <- "_s));
}

namespace QcProgressionRules {

QList<QcProgressionRule> ofContent(const QcContentRegistry &content)
{
    return RuleBuilder(content).build();
}

QString nodeOf(const QcProgressionGoal &goal)
{
    switch (goal.kind) {
    case QcProgressionGoal::Kind::Item:
        return Node::item(goal.target);
    case QcProgressionGoal::Kind::Food:
        return Node::food();
    case QcProgressionGoal::Kind::Fire:
        return Node::lighter();
    case QcProgressionGoal::Kind::Portal:
        return Node::portal(goal.target);
    case QcProgressionGoal::Kind::Dimension:
        break;
    }
    return Node::dimension(goal.target);
}

QList<QcProgressionResult> check(const QcContentRegistry &content, const QList<QcProgressionRule> &rules, const QList<QcWorldCensus> &censuses,
                                 const QString &startDimension)
{
    QList<QcProgressionRule> all = rules;
    for (const QcWorldCensus &census : censuses) {
        all.append(census.rules());
    }

    const QcProgressionSolver solver(std::move(all), {Node::dimension(startDimension)});
    QList<QcProgressionResult> results;
    for (const QcProgressionGoal &goal : content.worldgen().progressionGoals()) {
        results.append(resultOf(goal, solver));
    }
    return results;
}

} // namespace QcProgressionRules
