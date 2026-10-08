#include "QcCrafting.h"

#include <algorithm>

namespace QcCrafting {

namespace {

QcItemId itemAt(const QcContentRegistry &content, const QList<QcItemStack> &grid, int size, int column, int row)
{
    const QcItemStack &stack = grid[row * size + column];
    return !stack.isEmpty() && content.isValidItem(stack.item) ? stack.item : qcNoItem;
}

// An empty cell fits an empty ingredient only.
bool cellMatches(const QcContentRegistry &content, QcItemId item, const QString &wanted)
{
    return wanted.isEmpty() ? item == qcNoItem : item != qcNoItem && matches(content, item, wanted);
}

bool matchesShaped(const QcContentRegistry &content, const QcRecipeDefinition &recipe, const QList<QcItemStack> &grid,
                   int size)
{
    const int width = recipe.width();
    const int height = recipe.height();
    for (int offsetY = 0; offsetY + height <= size; ++offsetY) {
        for (int offsetX = 0; offsetX + width <= size; ++offsetX) {
            for (const bool mirrored : {false, true}) {
                bool ok = true;
                for (int row = 0; row < size && ok; ++row) {
                    for (int column = 0; column < size && ok; ++column) {
                        const int px = column - offsetX;
                        const int py = row - offsetY;
                        const bool inside = px >= 0 && px < width && py >= 0 && py < height;
                        const QString wanted = inside ? recipe.itemAt(mirrored ? width - 1 - px : px, py) : QString();
                        ok = cellMatches(content, itemAt(content, grid, size, column, row), wanted);
                    }
                }
                if (ok) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool matchesShapeless(const QcContentRegistry &content, const QcRecipeDefinition &recipe, const QList<QcItemStack> &grid,
                      int size)
{
    QList<QcItemId> present;
    for (int i = 0; i < size * size; ++i) {
        const QcItemId item = itemAt(content, grid, size, i % size, i / size);
        if (item != qcNoItem) {
            present.append(item);
        }
    }
    if (present.size() != recipe.ingredients.size()) {
        return false;
    }
    // Exact names first, so a tag never takes the item a named ingredient needs.
    QList<QString> wanted = recipe.ingredients;
    std::ranges::stable_partition(wanted, [](const QString &ingredient) { return !ingredient.startsWith(u'#'); });
    for (const QString &ingredient : std::as_const(wanted)) {
        const auto found = std::ranges::find_if(present, [&](QcItemId item) { return matches(content, item, ingredient); });
        if (found == present.end()) {
            return false;
        }
        present.erase(found);
    }
    return true;
}

} // namespace

std::optional<qsizetype> match(const QcContentRegistry &content, const QList<QcItemStack> &grid, int size)
{
    if (grid.size() < qsizetype(size) * size) {
        return std::nullopt;
    }
    const QList<QcRecipeDefinition> &recipes = content.recipes();
    for (qsizetype i = 0; i < recipes.size(); ++i) {
        const QcRecipeDefinition &recipe = recipes[i];
        if (recipe.shaped ? (recipe.width() <= size && recipe.height() <= size && matchesShaped(content, recipe, grid, size))
                          : matchesShapeless(content, recipe, grid, size)) {
            return i;
        }
    }
    return std::nullopt;
}

QList<std::pair<QString, int>> requirements(const QcRecipeDefinition &recipe)
{
    QList<std::pair<QString, int>> needed;
    const auto add = [&needed](const QString &item) {
        const auto it = std::ranges::find(needed, item, &std::pair<QString, int>::first);
        if (it != needed.end()) {
            ++it->second;
        } else {
            needed.append({item, 1});
        }
    };
    if (recipe.shaped) {
        for (int row = 0; row < recipe.height(); ++row) {
            for (int column = 0; column < recipe.width(); ++column) {
                const QString item = recipe.itemAt(column, row);
                if (!item.isEmpty()) {
                    add(item);
                }
            }
        }
    } else {
        for (const QString &item : recipe.ingredients) {
            add(item);
        }
    }
    std::ranges::stable_partition(needed, [](const std::pair<QString, int> &entry) { return !entry.first.startsWith(u'#'); });
    return needed;
}

bool consume(const QcContentRegistry &content, QcInventory &inventory, const QcRecipeDefinition &recipe, int times)
{
    const QList<std::pair<QString, int>> needed = requirements(recipe);
    return std::ranges::all_of(needed, [&](const std::pair<QString, int> &entry) {
        return removeIngredient(content, inventory, entry.first, entry.second * times);
    });
}

int timesCraftable(const QcContentRegistry &content, const QcInventory &inventory, const QcRecipeDefinition &recipe, int limit)
{
    const QList<std::pair<QString, int>> needed = requirements(recipe);
    int most = limit;
    for (const auto &[ingredient, count] : needed) {
        most = std::min(most, available(content, inventory, ingredient) / count);
    }
    if (most <= 0 || std::ranges::none_of(needed, [](const auto &entry) { return entry.first.startsWith(u'#'); })) {
        return std::max(0, most);
    }
    // A tag may share items with other ingredients: find the largest count consume() manages.
    int low = 0;
    int high = most;
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        QcInventory attempt = inventory;
        if (consume(content, attempt, recipe, middle)) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

bool matches(const QcContentRegistry &content, QcItemId item, const QString &ingredient)
{
    if (!content.isValidItem(item)) {
        return false;
    }
    const QcItemDefinition &definition = content.item(item);
    return ingredient.startsWith(u'#') ? definition.tags.contains(ingredient.mid(1)) : definition.name == ingredient;
}

int available(const QcContentRegistry &content, const QcInventory &inventory, const QString &ingredient)
{
    int count = 0;
    for (const QcItemStack &stack : inventory.allSlots()) {
        if (!stack.isEmpty() && matches(content, stack.item, ingredient)) {
            count += stack.count;
        }
    }
    return count;
}

bool removeIngredient(const QcContentRegistry &content, QcInventory &inventory, const QString &ingredient, int count)
{
    QList<QcItemId> fitting;
    for (const QcItemStack &stack : inventory.allSlots()) {
        if (!stack.isEmpty() && !fitting.contains(stack.item) && matches(content, stack.item, ingredient)) {
            fitting.append(stack.item);
        }
    }
    int missing = count;
    for (const QcItemId item : std::as_const(fitting)) {
        const int take = std::min(missing, inventory.countOf(item));
        if (take > 0 && inventory.removeItems(item, take)) {
            missing -= take;
        }
    }
    return missing <= 0;
}

int gridSizeNeeded(const QcRecipeDefinition &recipe)
{
    if (recipe.shaped) {
        return std::max(recipe.width(), recipe.height()) <= 2 ? 2 : 3;
    }
    return recipe.ingredients.size() <= 4 ? 2 : 3;
}

} // namespace QcCrafting
