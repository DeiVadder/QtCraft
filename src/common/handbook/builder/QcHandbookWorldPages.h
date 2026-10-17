#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QList>

struct QcHandbookContext;

// Handbook pages of the world generation, in this order: the biomes (category "biomes"), then the
// terrain overview, the ore veins and the features (category "worldgen"), each in content order.
namespace QcHandbookWorldPages {

QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookWorldPages
