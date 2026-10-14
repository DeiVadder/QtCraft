#pragma once

#include "QcEntityRecord.h"

#include <QStringList>

class QcEntities;

// Turns the entities of a running world into the records saved per chunk and back (decision E-366).
namespace QcEntityRecords {

// A record for every chunk with entities to keep, sorted by x, then z; each entry with its place in
// its kind's table (items first, then the drops waiting to appear).
QcWorldEntityRecords collect(const QcEntities &entities);
// Puts the saved entities into the world, each kind in its saved order, with new ids given to items,
// falling blocks, creatures and stands in that order. The limits of the kinds apply as when they are
// made (items beyond the total limit wait, falling blocks and stands beyond theirs are left out);
// returns the warnings about what was left out.
QStringList apply(const QcWorldEntityRecords &saved, QcEntities &entities);

} // namespace QcEntityRecords
