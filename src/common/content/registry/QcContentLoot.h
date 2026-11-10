// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/recipes/QcLootTables.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QString>

class QcContentRegistry;

/*! \brief Loot tables drawn with the items of a content (decisions E-521, E-1106): what chests,
    bartering and creature drops share. */
namespace QcContentLoot {

/*! Returns the lookup that resolves an item name of \a content to its id and largest stack
    (std::nullopt for an unknown item). \a content must outlive the lookup. */
QcLootTables::ItemLookup itemsOf(const QcContentRegistry &content);
/*! Returns the stacks one draw of the loot table named \a table of \a content gives for
    \a seed (the same seed gives the same stacks); nothing for an unknown table. */
QList<QcItemStack> draw(const QcContentRegistry &content, const QString &table, quint64 seed);

} // namespace QcContentLoot
