#pragma once

#include "QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

// Handbook pages of the items without a block page (categories "items", "tools" and "food"), in
// content order.
namespace QcHandbookItemPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookItemPages
