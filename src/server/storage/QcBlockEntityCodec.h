#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"
#include "server/storage/QcBlockEntityRecord.h"

#include <QByteArray>
#include <QSet>
#include <QStringList>

// The block entities of one chunk as a blob of its region file (blockentities/r.<x>.<z>.qcb;
// decisions E-358, E-373): containers and machines with their contents, saplings, what circuit
// components remember, scheduled ticks, positions the circuits wait to look at and pending block
// updates. Items are stored by name.
//
// The blob is a list of tables framed like the components of an entity (QcComponentFrames, decision
// E-440): `quint16 id, quint16 version, quint32 length, payload`, one table per kind
// (QcBlockEntityRecord::Table); an empty table is left out. A table holds a count and its entries;
// the entries of kinds with fields are framed again as `quint16 version, quint32 length, fields`, so
// an entry can grow by appending fields. Tables of unknown ids and what a newer version appends (to
// a table or to an entry) are kept in the record (QcBlockEntityRecord::kept) and written back
// unchanged.
namespace QcBlockEntityCodec {

QByteArray encode(const QcBlockEntityRecord &record, const QcContentRegistry &content);
// The blob of `record` within `limit` bytes, so its region file can read it again
// (QcRegionFile::maxBlobBytes): beyond the limit it is encoded again without the parts kept for a
// newer version or a mod (with a warning); still beyond it, an error, and the caller keeps the blob
// stored before.
[[nodiscard]] QcResult<QByteArray> encodeWithin(const QcBlockEntityRecord &record, const QcContentRegistry &content, QStringList &warnings,
                                                quint32 limit);
// The record of `chunk`. Unknown items are dropped with a warning (once per name in `reported`), so
// are unknown tables beyond QcComponentFrames::maxKept and the tails of entries of a newer version
// beyond one per cell of the chunk; damaged or implausible data (a position
// outside the chunk or twice, unknown priorities, a frame longer than the data, a table twice,
// version 0, an entry of this version longer or shorter than its fields) is an error. Values out of
// range are cut.
[[nodiscard]] QcResult<QcBlockEntityRecord> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                     QStringList &warnings, QSet<QString> &reported);

} // namespace QcBlockEntityCodec
