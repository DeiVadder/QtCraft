#include "QcRecipeDefinition.h"

QString QcRecipeDefinition::itemAt(int column, int row) const
{
    const QChar key = pattern[row][column];
    for (const auto &[character, item] : keys) {
        if (character == key) {
            return item;
        }
    }
    return {};
}
