#pragma once

#include "common/handbook/QcHandbookEntry.h"

#include <QList>

class QcContentRegistry;
struct QcBlockDefinition;
struct QcHandbookContext;

// Handbook tables built from the block data (category "tables", after the tables of
// QcHandbookOverviewPages, in this order): the Signal Dust components with what they take and give,
// the blocks that are no opaque full cubes with their light and spawning behaviour, the light
// sources and the blast resistance of every block (decision E-685).
namespace QcHandbookBlockTables {

// How much light a block takes away. Partial shapes block light only at the sides they cover fully
// (`coversSides`, decision E-104) or not at all, whatever their own value says.
struct LightBlocking
{
    bool coversSides = false;
    // Light levels taken away when passing through, 0..15 (15 blocks all light).
    int opacity = 15;
};

LightBlocking lightBlocking(const QcContentRegistry &content, const QcBlockDefinition &block);

QList<QcHandbookEntry> build(const QcHandbookContext &context);

} // namespace QcHandbookBlockTables
