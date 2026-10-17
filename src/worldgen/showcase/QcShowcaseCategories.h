#pragma once

#include "common/content/registry/QcContentRegistry.h"

#include <QList>

// Sorts the blocks of the registry into the showcase categories (decision E-220): every visible
// block goes to the first category whose match holds, else to the last category.
namespace QcShowcaseCategories {

bool matches(const QcShowcaseMatch &match, const QcBlockDefinition &block);
// Index of the category of `block`; 0 when there are no categories.
int categoryOf(const QcBlockDefinition &block, const QcContentRegistry &content);
// Block type indices (into content.blocks()) per category, in registry order; air and invisible
// blocks are left out.
QList<QList<quint32>> sort(const QcContentRegistry &content);

} // namespace QcShowcaseCategories
