// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/storage/QcEntityRecord.h"

#include <QHash>
#include <QSet>
#include <QStringList>

class QcEntities;

/*! \brief Turns the entities of a running world into the records saved per chunk and back
    (decisions E-366, E-374). */
namespace QcEntityRecords {

/*! Returns a record for every chunk of \a entities with entities to keep (persistent creatures,
    items and waiting drops, falling blocks, stands), each entry with its place in its kind's
    table (items first, then the drops waiting to appear). \c {populated} is left to the caller. */
QHash<QcChunkPos, QcEntityRecord> collect(const QcEntities &entities);
/*! Returns the same for the chunks in \a chunks, and the entities are taken out of the world:
    the records of chunks that leave it (from \a entities). Creatures that are not persistent and
    sliding blocks go without a record. */
QHash<QcChunkPos, QcEntityRecord> take(QcEntities &entities, const QSet<QcChunkPos> &chunks);
/*! Puts the entities of the chunk \a record into the world \a entities, each kind in its saved
    order after those already there, with new ids given to items, falling blocks, creatures and
    stands in that order. The limits of the kinds apply as when they are made (items beyond the
    total limit wait, falling blocks and stands beyond theirs are left out); returns the warnings
    about what was left out. */
QStringList apply(const QcEntityRecord &record, QcEntities &entities);

} // namespace QcEntityRecords
