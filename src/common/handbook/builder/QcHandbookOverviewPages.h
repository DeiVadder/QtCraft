#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

// Handbook pages that gather content from all over: the item tags (category "tags", by tag name)
// followed by the tables of fuels, tools and machine recipes (category "tables", in that order).
namespace QcHandbookOverviewPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookOverviewPages
