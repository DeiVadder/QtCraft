#include "common/handbook/builder/QcHandbookRecipes.h"

#include "common/handbook/builder/QcHandbookIndex.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbookLinks.h"
#include "common/handbook/QcLinkList.h"

#include <algorithm>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;
using Recipe = QcHandbookEntry::Recipe;
using Section = QcHandbookEntry::Section;

QcHandbookRecipes::QcHandbookRecipes(const QcContentRegistry &content, const QcHandbookText &text,
                                     const QcHandbookLinks &links, const QcHandbookIndex &index)
    : m_content(content)
    , m_text(text)
    , m_links(links)
    , m_index(index)
{
}

Recipe QcHandbookRecipes::craftingRecipe(const QcRecipeDefinition &recipe) const
{
    Recipe result = recipe.shaped ? shapedGrid(recipe) : shapelessGrid(recipe);
    result.kind = Recipe::Kind::Crafting;
    result.name = recipe.name;
    result.shapeless = !recipe.shaped;

    result.result = m_links.itemLink(recipe.resultItem);
    result.resultCount = recipe.resultCount;
    result.note = m_text.label(result.needsTable ? (result.shapeless ? u"recipe.tableShapeless"_s : u"recipe.table"_s)
                                                 : (result.shapeless ? u"recipe.handShapeless"_s : u"recipe.hand"_s));
    return result;
}

Recipe QcHandbookRecipes::shapedGrid(const QcRecipeDefinition &recipe) const
{
    Recipe result;
    result.width = std::max(1, recipe.width());
    result.height = std::max(1, recipe.height());

    for (int row = 0; row < recipe.height(); ++row) {
        for (int column = 0; column < recipe.width(); ++column) {
            const QString item = recipe.itemAt(column, row);
            result.cells.append(item.isEmpty() ? Link{} : m_links.ingredientLink(item));
        }
    }

    result.needsTable = recipe.width() > 2 || recipe.height() > 2;
    return result;
}

Recipe QcHandbookRecipes::shapelessGrid(const QcRecipeDefinition &recipe) const
{
    Recipe result;
    const int count = int(recipe.ingredients.size());
    result.width = count <= 1 ? 1 : (count <= 4 ? 2 : 3);
    result.height = (count + result.width - 1) / result.width;

    for (const QString &ingredient : recipe.ingredients) {
        result.cells.append(m_links.ingredientLink(ingredient));
    }
    while (result.cells.size() < qsizetype(result.width) * result.height) {
        result.cells.append(Link{});
    }
    result.needsTable = count > 4;
    return result;
}

Recipe QcHandbookRecipes::machineRecipe(const QcSmeltingRecipe &recipe) const
{
    Recipe result;
    result.kind = Recipe::Kind::Machine;
    result.name = recipe.name;
    result.cells = {m_links.itemLink(recipe.inputItem)};
    result.result = m_links.itemLink(recipe.resultItem);
    result.resultCount = recipe.resultCount;
    result.seconds = recipe.seconds;

    QStringList machineNames;
    // Machines that take every recipe of the category (the furnace) come before specialised ones.
    QList<qsizetype> machines = m_index.machinesByCategory(recipe.category);
    std::ranges::stable_partition(machines, [this](qsizetype index) {
        const QcBlockDefinition &machine = m_content.blocks()[index];
        return machine.machine.isMachine() && machine.machine.kinds.isEmpty();
    });

    for (const qsizetype index : std::as_const(machines)) {
        const QcBlockDefinition &machine = m_content.blocks()[index];
        const bool campfire = !machine.machine.isMachine();
        if (!usedBy(campfire ? machine.campfire.kinds : machine.machine.kinds, recipe)) {
            continue;
        }
        // A campfire cooks every recipe in its own time without fuel (decision E-772): it is linked,
        // but not named in the note about the machines' time and fuel.
        Link link = m_links.blockLink(machine.name);
        if (campfire) {
            link.note = m_text.seconds(machine.campfire.seconds);
        } else {
            link.note = machine.machine.speed != 1.0 ? m_text.label(u"unit.speed"_s).arg(m_text.number(machine.machine.speed)) : QString();
            machineNames.append(link.text);
        }
        result.machines.append(std::move(link));
    }

    if (machineNames.isEmpty()) {
        machineNames.append(m_text.machineName(recipe.category));
    }

    result.note = m_text.label(u"recipe.machine"_s).arg(machineNames.join(u", "_s), m_text.seconds(recipe.seconds));
    return result;
}

bool QcHandbookRecipes::usedBy(const QStringList &kinds, const QcSmeltingRecipe &recipe) const
{
    if (!qcAcceptsRecipeKind(kinds, recipe.kind)) {
        return false;
    }
    for (const QcSmeltingRecipe &earlier : m_content.smeltingRecipes()) {
        if (&earlier == &recipe) {
            return true;
        }
        if (earlier.category == recipe.category && earlier.inputItem == recipe.inputItem && qcAcceptsRecipeKind(kinds, earlier.kind)) {
            return false;
        }
    }
    return true;
}

QList<Section> QcHandbookRecipes::itemSections(const QString &item) const
{
    return madeSections(item) + usageSections(item);
}

QList<Section> QcHandbookRecipes::madeSections(const QString &item) const
{
    QList<Section> sections;
    const QList<QcRecipeDefinition> &recipes = m_content.recipes();
    const QList<QcSmeltingRecipe> &smelting = m_content.smeltingRecipes();

    Section made{m_text.label(u"section.crafting"_s), {}, {}, {}};
    const QList<qsizetype> crafted = m_index.recipesByResult(item);
    for (const qsizetype index : crafted) {
        made.recipes.append(craftingRecipe(recipes[index]));
    }
    if (!made.recipes.isEmpty()) {
        sections.append(std::move(made));
    }

    Section smelted{m_text.label(u"section.smelting"_s), {}, {}, {}};
    const QList<qsizetype> smeltedFrom = m_index.smeltingByResult(item);
    for (const qsizetype index : smeltedFrom) {
        smelted.recipes.append(machineRecipe(smelting[index]));
    }
    if (!smelted.recipes.isEmpty()) {
        smelted.links.append(m_links.tableLink(u"fuels"_s));
        sections.append(std::move(smelted));
    }

    Section smeltsInto{m_text.label(u"section.smeltsInto"_s), {}, {}, {}};
    const QList<qsizetype> smeltedInto = m_index.smeltingByInput(item);
    for (const qsizetype index : smeltedInto) {
        smeltsInto.recipes.append(machineRecipe(smelting[index]));
    }
    if (!smeltsInto.recipes.isEmpty()) {
        smeltsInto.links.append(m_links.tableLink(u"fuels"_s));
        sections.append(std::move(smeltsInto));
    }
    return sections;
}

QList<Section> QcHandbookRecipes::usageSections(const QString &item) const
{
    QList<Section> sections;
    const QList<QcRecipeDefinition> &recipes = m_content.recipes();

    QcLinkList usedBy;
    const QList<QcHandbookIndex::IngredientUse> uses = m_index.recipesByIngredient(item);
    for (const auto &[index, tag] : uses) {
        const QString note = tag.isEmpty() ? QString() : m_links.tagTitle(tag, m_text.language());
        usedBy.addUnique(m_links.itemLink(recipes[index].resultItem, note));
    }
    Section usedIn{m_text.label(u"section.usedIn"_s), {}, usedBy.sorted(), {}};
    if (!usedIn.links.isEmpty()) {
        sections.append(std::move(usedIn));
    }

    const QList<Link> droppedBy = QcHandbookLinks::sortedLinks(m_index.droppedBy(item));
    if (!droppedBy.isEmpty()) {
        sections.append({m_text.label(u"section.droppedBy"_s), {}, droppedBy, {}});
    }

    QList<Link> caughtFrom;
    QList<Link> catches;
    for (const QcMobDefinition &mob : m_content.mobs()) {
        if (mob.catchItem == item) {
            caughtFrom.append(m_links.mobLink(mob, m_links.itemTitle(mob.catchWith, m_text.language())));
        }
        if (mob.catchWith == item) {
            catches.append(m_links.mobLink(mob, m_links.itemTitle(mob.catchItem, m_text.language())));
        }
    }

    if (!caughtFrom.isEmpty()) {
        sections.append({m_text.label(u"section.caughtFrom"_s), {}, caughtFrom, {}});
    }
    if (!catches.isEmpty()) {
        sections.append({m_text.label(u"section.catches"_s), {}, catches, {}});
    }
    return sections;
}
