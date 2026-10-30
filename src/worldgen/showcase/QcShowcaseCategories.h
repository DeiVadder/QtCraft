// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QList>

/*! \brief Sorts the blocks of the registry into the showcase categories (decision E-220): every
    visible block goes to the first category whose match holds, else to the last category. */
namespace QcShowcaseCategories {

/*! Returns whether \a block meets every condition of \a match. */
bool matches(const QcShowcaseMatch &match, const QcBlockDefinition &block);
/*! Returns the index of the category of \a block among the showcase categories of \a content: the
    first whose match holds, else the last; 0 when there are no categories. */
int categoryOf(const QcBlockDefinition &block, const QcContentRegistry &content);
/*! Returns the block type indices (into the blocks of \a content) per category, in registry
    order; air and invisible blocks are left out. */
QList<QList<quint32>> sort(const QcContentRegistry &content);

} // namespace QcShowcaseCategories
