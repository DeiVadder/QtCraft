// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

/*! \brief Crafting recipe from content data.

    Shaped recipes have rows of single-character keys (space = empty); shapeless recipes list
    their ingredients. */
struct QcRecipeDefinition
{
    /*! Qualified name (\c {"name"}, required). */
    QString name;
    /*! Whether it has a pattern (\c {"type": "shaped"}, the default) or lists its ingredients
        (\c {"shapeless"}). */
    bool shaped = true;
    /*! Rows of the pattern after trimming, all of equal width. */
    QList<QString> pattern;
    /*! Item name per pattern key character, in key order. */
    QList<std::pair<QChar, QString>> keys;
    /*! Ingredients of a shapeless recipe (\c {"ingredients"}, 1 to 9): qualified item names or
        "#tag". */
    QList<QString> ingredients;
    /*! Qualified name of the item it makes (\c {"item"} in \c {"result"}, required). */
    QString resultItem;
    /*! Items it makes (\c {"count"} in \c {"result"}, 1 to 64). */
    int resultCount = 1;

    /*! Returns the width of the pattern; 0 without one. */
    int width() const { return pattern.isEmpty() ? 0 : static_cast<int>(pattern.first().size()); }
    /*! Returns the number of pattern rows. */
    int height() const { return static_cast<int>(pattern.size()); }
    /*! Returns the item name at the pattern cell in \a column and \a row, empty for a blank
        cell. Both must lie inside the pattern. */
    QString itemAt(int column, int row) const;
};
