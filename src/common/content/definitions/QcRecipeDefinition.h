#pragma once

#include <QList>
#include <QString>

// Crafting recipe from content data. Shaped recipes have rows of single-character keys (space = empty);
// shapeless recipes list their ingredients.
struct QcRecipeDefinition
{
    QString name;
    bool shaped = true;
    // Rows of the pattern after trimming, all of equal width.
    QList<QString> pattern;
    // Item name per pattern key character, in key order.
    QList<std::pair<QChar, QString>> keys;
    QList<QString> ingredients;
    QString resultItem;
    int resultCount = 1;

    int width() const { return pattern.isEmpty() ? 0 : static_cast<int>(pattern.first().size()); }
    int height() const { return static_cast<int>(pattern.size()); }
    // Item name at a pattern cell, empty for a blank cell.
    QString itemAt(int column, int row) const;
};
