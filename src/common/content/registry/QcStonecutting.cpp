// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcStonecutting.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcLogging.h"

#include <QHash>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

// A part the stonecutter makes from a base block, and how many.
struct Cut
{
    QString part;
    int count = 1;
};

// The name of the recipe cutting `input` into `part`: "<part>_from_<input path>" when both share a
// namespace, else "<part>_from_<input namespace>:<input path>". A path never holds the ':', so two
// inputs of different packs never share a name (with '_' pack "a_b" path "c" met pack "a" path "b_c").
QString recipeName(const QString &input, const QString &part)
{
    const qsizetype colon = input.indexOf(u':');
    const bool samePack = colon >= 0 && part.startsWith(input.left(colon + 1));
    const QString from = samePack ? input.mid(colon + 1) : input;
    return u"%1_from_%2"_s.arg(part, from);
}

// Adds the recipe recipeName() unless a pack defined one under that name.
void addRecipe(QcContentRegistry &registry, const QString &input, const Cut &cut)
{
    const QString name = recipeName(input, cut.part);
    if (registry.stonecuttingIndex(name) || !registry.itemId(input) || !registry.itemId(cut.part)) {
        return;
    }
    // Long names of two packs together would not fit into the protocol's messages.
    if (name.size() > QcStonecutting::maxRecipeName) {
        qCWarning(lcRegistry) << "Stonecutting recipe" << name << "skipped: its name is longer than" << QcStonecutting::maxRecipeName
                              << "characters";
        return;
    }

    QcSmeltingRecipe recipe;
    recipe.name = name;
    recipe.category = QString::fromLatin1(QcSmeltingRecipe::stonecuttingCategory);
    recipe.inputItem = input;
    recipe.resultItem = cut.part;
    recipe.resultCount = cut.count;
    registry.registerStonecuttingRecipe(std::move(recipe));
}

// The parts the stonecutter makes from each base block, in the order of the blocks.
QList<std::pair<QString, QList<Cut>>> cutsByBase(const QcContentRegistry &registry)
{
    QHash<QString, const QcPartFamily *> families;
    for (const QcPartFamily &family : registry.partFamilies()) {
        families.insert(family.name, &family);
    }
    QHash<QString, const QcMaterialDefinition *> materials;
    for (const QcMaterialDefinition &material : registry.materials()) {
        materials.insert(material.name, &material);
    }

    QList<std::pair<QString, QList<Cut>>> cuts;
    QHash<QString, qsizetype> index;
    for (const QcBlockDefinition &block : registry.blocks()) {
        // A part's `partOf` is "<material>/<family>".
        const qsizetype slash = block.partOf.lastIndexOf(u'/');
        const QcPartFamily *family = slash < 0 ? nullptr : families.value(block.partOf.mid(slash + 1));
        const QcMaterialDefinition *material = slash < 0 ? nullptr : materials.value(block.partOf.left(slash));
        if (!family || !material || family->stonecutterCount == 0 || !family->stonecutterKinds.contains(material->kind)) {
            continue;
        }
        if (!index.contains(material->block)) {
            index.insert(material->block, cuts.size());
            cuts.append({material->block, {}});
        }
        cuts[index.value(material->block)].second.append({block.name, family->stonecutterCount});
    }
    return cuts;
}

} // namespace

namespace QcStonecutting {

void generate(QcContentRegistry &registry)
{
    const QList<std::pair<QString, QList<Cut>>> cuts = cutsByBase(registry);
    QHash<QString, QList<Cut>> byBase;
    for (const auto &[base, parts] : cuts) {
        byBase.insert(base, parts);
        for (const Cut &cut : parts) {
            addRecipe(registry, base, cut);
        }
    }

    // A copy: the loop registers recipes. Blocks cut one to one into a base block are cut into its
    // parts too.
    const QList<QcSmeltingRecipe> defined = registry.stonecuttingRecipes();
    for (const QcSmeltingRecipe &recipe : defined) {
        if (recipe.resultCount != 1) {
            continue;
        }
        const QList<Cut> parts = byBase.value(recipe.resultItem);
        for (const Cut &cut : parts) {
            addRecipe(registry, recipe.inputItem, cut);
        }
    }
}

QList<qsizetype> recipesFor(const QcContentRegistry &content, const QString &inputItem)
{
    QList<qsizetype> found;
    const QList<QcSmeltingRecipe> &recipes = content.stonecuttingRecipes();
    for (qsizetype i = 0; i < recipes.size(); ++i) {
        if (recipes[i].inputItem == inputItem) {
            found.append(i);
        }
    }
    return found;
}

QcItemStack output(const QcContentRegistry &content, const QcItemStack &input, const QString &recipe)
{
    const std::optional<qsizetype> index = recipe.isEmpty() ? std::nullopt : content.stonecuttingIndex(recipe);
    if (input.isEmpty() || !index || !content.isValidItem(input.item)) {
        return {};
    }

    const QcSmeltingRecipe &found = content.stonecuttingRecipes()[*index];
    const std::optional<QcItemId> result = content.itemId(found.resultItem);
    if (found.inputItem != content.item(input.item).name || !result) {
        return {};
    }
    return {*result, found.resultCount, 0};
}

} // namespace QcStonecutting
