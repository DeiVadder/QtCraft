#pragma once

#include "QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

// Handbook pages of the creatures (category "creatures"), in content order: behaviour, spawning
// and drops.
namespace QcHandbookCreaturePages {

QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookCreaturePages
