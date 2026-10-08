#include "QcCrafting.h"

#include <algorithm>

namespace QcCrafting {

namespace {

QString nameAt(const QcContentRegistry &content, const QList<QcItemStack> &grid, int size, int column, int row)
{
    const QcItemStack &stack = grid[row * size + column];
    return !stack.isEmpty() && content.isValidItem(stack.item) ? content.item(stack.item).name : QString();
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
                        ok = nameAt(content, grid, size, column, row) == wanted;
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
    QList<QString> present;
    for (int i = 0; i < size * size; ++i) {
        const QString name = nameAt(content, grid, size, i % size, i / size);
        if (!name.isEmpty()) {
            present.append(name);
        }
    }
    QList<QString> wanted = recipe.ingredients;
    std::ranges::sort(present);
    std::ranges::sort(wanted);
    return present == wanted;
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
    return needed;
}

int gridSizeNeeded(const QcRecipeDefinition &recipe)
{
    if (recipe.shaped) {
        return std::max(recipe.width(), recipe.height()) <= 2 ? 2 : 3;
    }
    return recipe.ingredients.size() <= 4 ? 2 : 3;
}

} // namespace QcCrafting
