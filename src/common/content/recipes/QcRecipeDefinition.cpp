// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/recipes/QcRecipeDefinition.h"

QString QcRecipeDefinition::itemAt(int column, int row) const
{
    if (row < 0 || row >= pattern.size() || column < 0 || column >= pattern[row].size()) {
        return {};
    }
    const QChar key = pattern[row][column];
    for (const auto &[character, item] : keys) {
        if (character == key) {
            return item;
        }
    }
    return {};
}
